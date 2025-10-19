//
// This file provided as part of the DataContainer project (see the license in /parser folder)
// adjusted for the needs of DataContainer-Lua:
// 1) generates code to be included into c++ executables instead of dll
// 2) generates bindings/annotations in Lua as well
// 3) accesses pointer to dcon state instead of global dcon state
//

#include <cassert>
#include <cctype>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <set>

#include "parsing.hpp"

static std::set<std::string> made_types;
static std::string game_state;

enum class meta_information {
	id, value, value_pointer, empty
};

enum class array_access {
	function_call, get_call, set_call, resize_call, size_call
};

enum class lua_type_match {
	fat_float, integer, floating_point, boolean, lua_object, handle_to_integer, opaque
};
struct combotype {
	lua_type_match normalized;
	std::string c_type;
	std::string api_type;
	std::string lua_type;
};

struct arg_information {
	meta_information meta_type;
	combotype type;
	std::string name;
};
struct function_call_information {
	array_access access_type;
	std::string project_prefix;
	std::string accessed_object;
	std::string accessed_property;
	std::vector<arg_information> in;
	arg_information out;
};

void error_to_file(std::string const& file_name) {
	std::fstream fileout;
	fileout.open(file_name, std::ios::out);
	if(fileout.is_open()) {
		fileout << "";
		fileout.close();
	}
}

relationship_object_def const* better_primary_key(relationship_object_def const* oldr, relationship_object_def const* newr) {
	if(oldr == nullptr) {
		return newr;
	}

	if(oldr->is_expandable && !newr->is_expandable)
		return newr;
	if(!oldr->is_expandable && newr->is_expandable)
		return oldr;

	switch(oldr->store_type) {
		case storage_type::contiguous:
		{
			switch(newr->store_type) {
				case storage_type::contiguous:
					return oldr->size <= newr->size ? oldr : newr;
				case storage_type::compactable:
				case storage_type::erasable:
					return oldr;
			}
			break;
		}
		case storage_type::erasable:
		{
			switch(newr->store_type) {
				case storage_type::contiguous:
					return newr;
				case storage_type::erasable:
					return oldr->size <= newr->size ? oldr : newr;
				case storage_type::compactable:
					return oldr;
			}
			break;
		}
		case storage_type::compactable:
		{
			switch(newr->store_type) {
				case storage_type::contiguous:
				case storage_type::erasable:
					return newr;
				case storage_type::compactable:
					return oldr->size <= newr->size ? oldr : newr;
			}
			break;
		}
	}

	return oldr;
}


std::string convert_lua_enum_to_type(lua_type_match v) {
	switch (v){
	case lua_type_match::integer:
		return "number";
	case lua_type_match::floating_point:
		return "number";
	case lua_type_match::fat_float:
		return "number";
	case lua_type_match::boolean:
		return "boolean";
	case lua_type_match::lua_object:
		return "table";
	case lua_type_match::handle_to_integer:
		return "number";
	case lua_type_match::opaque:
		return "table";
	}
}



std::string convert_to_id(std::string in) {
	if (made_types.count(in) > 0) {
		return in;
	} else if (made_types.count(in + "_id") > 0) {
		return in + "_id";
	}
	return in;
}

combotype normalize_type(std::string const& in, std::set<std::string> const& made_types);

combotype normalize_type(std::string const& in, std::set<std::string> const& made_types) {
	if (
		in == "char"
		|| in == "unsigned char"
		|| in == "bool"
		|| in == "int8_t"
		|| in == "uint8_t"
		|| in == "signed char"
		|| in == "short"
		|| in == "int16_t"
		|| in == "uint16_t"
		|| in == "unsigned short"
		|| in == "int"
		|| in == "long"
		|| in == "unsigned int"
		|| in == "unsigned long"
		|| in == "int32_t"
		|| in == "uint32_t"
		|| in == "size_t"
		|| in == "unsigned long long"
		|| in == "int64_t"
		|| in == "uint64_t"
		|| in == "long long"
		|| in == "float"
		|| in == "double"
	)
		return {
			lua_type_match::fat_float,
			in,
			in,
			"number"
		};
	if(in == "bitfield")
		return {
			lua_type_match::boolean,
			in,
			"bool",
			"boolean"
		};
	if(in == "lua_reference_type")
		return {
			lua_type_match::lua_object,
			in,
			"int32_t",
			"number"
		};
	// for(auto& mi : file.extra_ids) {
	// 	if (mi.name == in) {
	// 		return normalize_type(file, mi.base_type, made_types);
	// 	}
	// }

	if(made_types.count(in) + made_types.count(in + "_id") != 0){
		std::string lua_type = "???";
		if (made_types.count(in) > 0) {
			lua_type = in;
		} else if (made_types.count(in + "_id") > 0) {
			lua_type = in + "_id";
		} else {
			lua_type = "number";
		}
		return {
			lua_type_match::handle_to_integer,
			in,
			"int32_t",
			lua_type
		};
	}

	return {
		lua_type_match::opaque,
		in,
		in,
		"table"
	};
}

std::string convert_raw_to_id (file_def& file, std::string object_name, std::string raw_id) {

	return file.namspace + "::" + object_name + "_id{" + file.namspace + "::" + object_name + "_id::value_base_t(" + raw_id + ")}";
}

std::string convert_raw_to_id_from_id (file_def& file, std::string id_type_name, std::string raw_id) {

	return file.namspace + "::" + id_type_name + "{" + file.namspace + "::" + id_type_name + "::value_base_t(" + raw_id + ")}";
}

std::string convert_raw_to_index (file_def& file, std::string index_type, std::string raw_index) {
	auto norm_index_type = normalize_type(index_type, made_types);
	std::string index_access_string;
	if(norm_index_type.normalized == lua_type_match::handle_to_integer) {
		index_access_string = convert_raw_to_id_from_id(file, index_type, raw_index);
	} else {
		index_access_string = "(" + norm_index_type.c_type + ")(" + raw_index + ");\n";
	}
	return index_access_string;
}

std::string declare_id_from_raw (std::string indent, file_def& file, std::string object_name, std::string raw_id, std::string id) {
	return indent + "auto " + id + " = " + convert_raw_to_id(file, object_name, raw_id) + ";\n";
}

std::string declare_id_from_raw_id (std::string indent, file_def& file, std::string object_name, std::string raw_id, std::string id) {
	return indent + "auto " + id + " = " + convert_raw_to_id_from_id(file, object_name, raw_id) + ";\n";
}

std::string declare_index_from_raw (std::string indent, file_def& file, std::string index_type, std::string raw_index, std::string index) {

	return indent + "auto " + index + " = " + convert_raw_to_index(file, index_type, raw_index) + ";\n";
}

std::string access_property_name(
	function_call_information desc
	// std::string object_name, std::string project_prefix, std::string property
) {
	std::string property = desc.accessed_property;
	// replace for vector pools
	if (desc.access_type == array_access::get_call) {
		property = "get_" + property;
	} else if (desc.access_type == array_access::set_call) {
		property = "set_" + property;
	} else if (desc.access_type == array_access::resize_call) {
		property = "size_" + property;
	} else if (desc.access_type == array_access::size_call) {
		property = "resize_" + property;
	}
	return desc.project_prefix + desc.accessed_object + "_" + property;
}

std::string access_core_property_name(
	std::string object_name, std::string property
) {
	return game_state + object_name + "_" + property;
}


arg_information normalize_argument(std::string name, bool is_bool, std::string& declared_type) {
	if (made_types.count(declared_type) + made_types.count(declared_type + "_id") > 0) {
		return {
			meta_information::id,
			normalize_type(declared_type, made_types),
			name
		};
	} else {
		arg_information arg {
			meta_information::value,
			normalize_type(declared_type, made_types),
			name
		};
		if (is_bool) {
			arg.type.normalized = lua_type_match::boolean;
			arg.type.c_type = "bool";
			arg.type.lua_type = "boolean";
			arg.type.api_type = "bool";
		}
		if (arg.type.normalized == lua_type_match::opaque) {
			// assert(false);
			arg.meta_type = meta_information::value_pointer;
		};
		return arg;
	}
}

std::string to_string(meta_information type_desc) {
	switch (type_desc) {
	case meta_information::id:
		return "id";
	case meta_information::value:
		return "value";
	case meta_information::value_pointer:
		return "value_ptr";
	case meta_information::empty:
		return "void";
	}
}

std::string to_string(arg_information type_desc) {
	switch (type_desc.meta_type) {
	case meta_information::id:
		return "int32_t";
	case meta_information::value:
		return type_desc.type.api_type;
	case meta_information::value_pointer:
		return type_desc.type.c_type + "*";
	case meta_information::empty:
		return "void";
	}
}



std::string api_arg_string(arg_information& arg, int counter) {
	return "api_arg_" + std::to_string(counter) + "_" + to_string(arg.meta_type);
}

std::string container_arg_string(arg_information& arg, int counter) {
	return "container_arg_" + std::to_string(counter) + "_" + to_string(arg.meta_type);
}

std::string intermediate_type(arg_information& type_desc) {
	switch (type_desc.meta_type) {
	case meta_information::id:
		return "dcon::" + type_desc.type.c_type;
	case meta_information::value:
		return type_desc.type.c_type;
	case meta_information::value_pointer:
		return type_desc.type.c_type + "*";
	case meta_information::empty:
		return "void";
	}
}

std::string generate_head(function_call_information desc) {
	std::string result = "";
	result += to_string(desc.out);
	result += " ";
	result += access_property_name(desc);
	result += "(";
	auto counter = 0;
	for (auto& item : desc.in) {
		result += item.type.api_type;
		result += " ";
		result += api_arg_string(item, counter);
		result += ", ";
		counter++;
	}
	if (counter > 0) {
		result.pop_back();
		result.pop_back();
	}
	result += ")";

	return result;
}

auto generate_body(file_def& file, function_call_information desc) {
	std::string result = "";

	result += "{\n";

	std::string container_args = "";

	{
		int counter = 0;
		for (auto& item : desc.in) {
			result += "\t";
			result += intermediate_type(item);
			result += " ";
			result += container_arg_string(item, counter);
			result += " = ";
			if (item.meta_type == meta_information::id) {
				result += convert_raw_to_id_from_id(file, item.type.c_type, api_arg_string(item, counter));
			} else if (item.meta_type == meta_information::value) {
				result += api_arg_string(item, counter);
			} else if (item.meta_type == meta_information::value_pointer) {
				result += "&" + api_arg_string(item, counter);
			} else {
				assert("false");
			}
			result += ";\n";
			counter++;
		}
	}

	result += "\t";
	if (desc.access_type == array_access::function_call) {
		std::string call = access_core_property_name(desc.accessed_object, desc.accessed_property);
		std::string args = "";
		for (size_t i = 0; i < desc.in.size(); i++) {
			args += container_arg_string(desc.in[i], i);
			if (i + 1 < desc.in.size()) {
				args += ", ";
			}
		}
		if (desc.out.meta_type == meta_information::empty) {
			result +=  call + "(" + args + ");\n";
		} else {
			switch (desc.out.meta_type) {

			case meta_information::id: {
				result += intermediate_type(desc.out);
				result += " result = ";
				result += call + "(" + args + ");\n";
				result += "\treturn result.index();\n";
				break;
			}
			case meta_information::value: {
				result += "return " + call + "(" + args + ");\n";
				break;
			}
			case meta_information::value_pointer: {
				result += "return &" + call + "(" + args + ");\n";
				break;
			}
			case meta_information::empty:
				break;
			}
		}
	} else if (desc.access_type == array_access::set_call) {
		assert(desc.in.size() == 3);
		result += game_state + desc.accessed_object + "_get_" + desc.accessed_property;
		result += "(";
		result += container_arg_string(desc.in[0], 0);
		result += ")";
		result += ".at(";
		result += container_arg_string(desc.in[1], 1);
		result += ") = ";
		result += container_arg_string(desc.in[2], 2);
		result += ";\n";
	} else if (desc.access_type == array_access::get_call) {
		assert(desc.in.size() == 2);
		std::string access_string = "";
		access_string += game_state + desc.accessed_object + "_get_" + desc.accessed_property;
		access_string += "(";
		access_string += container_arg_string(desc.in[0], 0);
		access_string += ").at(";
		access_string += container_arg_string(desc.in[1], 1);
		access_string += ")";

		switch (desc.out.meta_type) {

		case meta_information::id: {
			result += intermediate_type(desc.out);
			result += " result = ";
			result += access_string + ";\n";
			result += "\treturn result.index();\n";
			break;
		}
		case meta_information::value: {
			result += "return " + access_string + ";\n";
			break;
		}
		case meta_information::value_pointer: {
			result += "return &" + access_string + ";\n";
			break;
		}
		case meta_information::empty:
			break;
		}
	} else if (desc.access_type == array_access::resize_call) {
		assert(desc.in.size() == 2);
		assert(desc.out.meta_type == meta_information::empty);
		std::string access_string = "";
		access_string += game_state + desc.accessed_object + "_get_" + desc.accessed_property;
		access_string += "(";
		access_string += container_arg_string(desc.in[0], 0);
		access_string += ").resize(";
		access_string += container_arg_string(desc.in[1], 1);
		access_string += ")";
		result += access_string;
		result += ";\n";
	} else if (desc.access_type == array_access::size_call) {
		assert(desc.in.size() == 1);
		assert(desc.out.meta_type == meta_information::value);
		std::string access_string = "";
		access_string += game_state + desc.accessed_object + "_get_" + desc.accessed_property;
		access_string += "(";
		access_string += container_arg_string(desc.in[0], 0);
		access_string += ").size()";
		result += "return (" + access_string + ");\n";
	}

	result += "}\n";

	return result;
}

std::string lua_id(
	std::string object_name
) {
	return object_name;
}

/*

if description in ["uint32_t", "int32_t", "float", "uint16_t", "uint8_t"]:
self.c_type = description
self.lsp_type = "number"
self.dcon_type = description
return
# print(3)
if description == "bool":
self.c_type = "bool"
self.lsp_type = "boolean"
self.dcon_type = "bitfield"
return
# print(4)
if description in REGISTERED_ID_NAMES:
self.c_type = "int32_t"
self.lsp_type = description
self.dcon_type = description
return
# print(5)
if description in REGISTERED_NAMES:
self.c_type = "int32_t"
self.lsp_type = prefix_to_id_name(description)
self.dcon_type = description
return
# print(6)
if description in REGISTERED_STRUCTS:
self.c_type = description
self.lsp_type = "struct_" + description
self.dcon_type = "base_types::" + description
return

*/

std::string to_luatype(
	property_def& def
) {
	if (def.data_type == "uint32_t") { // numbers
		return "number";
	} else if (def.data_type  == "uint32_t") {
		return "number";
	} else if (def.data_type  == "int32_t") {
		return "number";
	} else if (def.data_type  == "uint16_t") {
		return "number";
	} else if (def.data_type  == "int16_t") {
		return "number";
	} else if (def.data_type  == "uint8_t") {
		return "number";
	} else if (def.data_type  == "int8_t") {
		return "number";
	} else if (def.data_type  == "float") {
		return "number";
	} else if (def.data_type  == "bitfield") { // boolean
		return "boolean";
	}

	auto flat_name = def.data_type;
	for (size_t i = 0; i < flat_name.length(); i++) {
		if (flat_name[i] == ':') {
			flat_name[i] = '_';
		}
	}
	return flat_name;
}


int main(int argc, char *argv[]) {
	if (argc < 5) {
		printf("[1]: PROJECT NAME, [2] DATA CONTAINER VARIABLE, [3]: DCON DEFINITION FILE, [4]: CPP OUTPUT FILE, [5]: HPP OUTPUT FILE, [6]: LUA OUTPUT FOLDER");
		return 1;
	}

	const std::string project_name = argv[1];
	const std::string project_prefix = project_name + "_";

	game_state = argv[2];

	std::fstream input_file;
	std::string input_file_name(argv[3]);
	input_file.open(argv[3], std::ios::in);

	const std::string dll_source_name = argv[4];
	const std::string dll_header_name = argv[5];
	const std::string lua_folder = argv[6];
	const std::string lua_dcon_path = lua_folder + "/dcon_generated";
	const std::string lua_manager_name = lua_folder + "/" + "manager.lua";


	const std::string base_include_name = [&]() {
		auto sep_pos = dll_header_name.find_last_of('\\');
		if(sep_pos == std::string::npos)
			sep_pos = dll_header_name.find_last_of('/');
		if(sep_pos == std::string::npos) {
			return dll_header_name;
		} else {
			return dll_header_name.substr(sep_pos + 1);
		}
	}();

	error_record err(input_file_name);

	if(!input_file.is_open()) {
		err.add(row_col_pair{ 0, 0}, 1000, "Could not open input file");
		std::cout << err.accumulated;
		return -1;
	}

	std::string file_contents((std::istreambuf_iterator<char>(input_file)), std::istreambuf_iterator<char>());

	file_def parsed_file = parse_file(file_contents.c_str(), file_contents.c_str() + file_contents.length(), err);

	input_file.close();

	if(err.accumulated.length() > 0) {
		error_to_file(dll_header_name);
		std::cout << err.accumulated;
		return -1;
	}

	// patchup relationship pointers & other information

	for(auto& r : parsed_file.relationship_objects) {
		if(r.is_relationship) {
			for(size_t j = 0; j < r.indexed_objects.size(); ++j) {
				related_object& relobj = r.indexed_objects[j];
				if(auto linked_object = find_by_name(parsed_file, relobj.type_name); linked_object) {
					relobj.related_to = linked_object;
				} else {
					err.add(row_col_pair{ 0, 0}, 1001, std::string("Could not find object named: ") + relobj.type_name + " in relationship: " + r.name);
					error_to_file(dll_header_name);
					std::cout << err.accumulated;
					return -1;
				}
				if(relobj.index == index_type::at_most_one && !relobj.is_optional && relobj.multiplicity == 1) {
					r.primary_key.points_to = better_primary_key(r.primary_key.points_to, relobj.related_to);
					if(r.primary_key.points_to == relobj.related_to)
						r.primary_key.property_name = relobj.property_name;
				}

				if(relobj.multiplicity > 1 && relobj.index == index_type::many && relobj.ltype == list_type::list) {
					err.add(row_col_pair{ 0, 0}, 1002, std::string("Unsupported combination of list type storage with multiplicity > 1 in link ")
						+ relobj.property_name + " in relationship: " + r.name);
					error_to_file(dll_header_name);
					std::cout << err.accumulated;
					return -1;
				}

				if(relobj.multiplicity > 1 && relobj.index == index_type::at_most_one) {
					relobj.is_distinct = true;
				}
			}



			if(r.indexed_objects.size() == 0) {
				err.add(row_col_pair{ 0, 0}, 1003, std::string("Relationship: ") + r.name + " is between too few objects");
				error_to_file(dll_header_name);
				std::cout << err.accumulated;
				return -1;
			}


			if(r.force_pk.length() > 0) {
				bool pk_forced = false;
				for(auto& link : r.indexed_objects) {
					if(link.property_name == r.force_pk && link.index == index_type::at_most_one
						&& !link.is_optional && link.multiplicity == 1) {
						r.primary_key.points_to = link.related_to;
						r.primary_key.property_name = link.property_name;
						pk_forced = true;
					}
				}
				if(!pk_forced) {
					err.add(row_col_pair{ 0, 0}, 1004, std::string("Was unable to use ") + r.force_pk + std::string(" as a primary key for relationship: ") + r.name);
					error_to_file(dll_header_name);
					std::cout << err.accumulated;
					return -1;
				}
			}

			for(auto& link : r.indexed_objects) {
				if(link.index != index_type::none)
					link.related_to->relationships_involved_in.push_back(in_relation_information{ r.name, &link, &r});
			}

			if(r.primary_key.points_to) {
				r.size = r.primary_key.points_to->size;
				r.store_type = storage_type::contiguous;
				r.is_expandable = r.primary_key.points_to->is_expandable;

				for(auto& l : r.indexed_objects) {
					if(r.primary_key == l) {
						l.is_primary_key = true;
					}
				}
			} else {
				if(r.store_type != storage_type::erasable && r.store_type != storage_type::compactable) {
					err.add(row_col_pair{ 0, 0}, 1005, std::string("Relationship ") + r.name +
						" has no primary key, and thus must have either a compactable or erasable storage type to provide a delete function.");
					error_to_file(dll_header_name);
					std::cout << err.accumulated;
					return -1;
				}
			}

		} // end if is a relationship
	} // patchup relationship pointers loop

	// compile list of objects that need serailization stubs
	std::vector<std::string> needs_serialize;
	std::vector<std::string> needs_load_only;

	for(auto& r : parsed_file.relationship_objects) {
		for(auto& p : r.properties) {
			if(p.type == property_type::object) {
				if(std::find(needs_serialize.begin(), needs_serialize.end(), p.data_type) == needs_serialize.end()) {
					needs_serialize.push_back(p.data_type);
					parsed_file.object_types.push_back(p.data_type);
				}
			}
		}
	}
	for(auto& lt : parsed_file.legacy_types) {
		if(std::find(needs_serialize.begin(), needs_serialize.end(), lt) == needs_serialize.end()
			&& std::find(needs_load_only.begin(), needs_load_only.end(), lt) == needs_load_only.end()) {
			needs_load_only.push_back(lt);
			parsed_file.object_types.push_back(lt);
		}
	}

	// identify vectorizable types
	for(auto& ob : parsed_file.relationship_objects) {
		for(auto& prop : ob.properties) {
			if(prop.type == property_type::other && is_vectorizable_type(parsed_file, prop.data_type))
				prop.type = property_type::vectorizable;
			if(prop.type == property_type::array_other && is_vectorizable_type(parsed_file, prop.data_type))
				prop.type = property_type::array_vectorizable;
		}
	}


	// patch up composite key info
	// bool needs_hash_include = false;
	std::vector<int32_t> byte_sizes_need_hash;

	for(auto& ob : parsed_file.relationship_objects) {
		for(auto& cc : ob.composite_indexes) {
			// needs_hash_include = true;

			int32_t bits_so_far = 0;
			for(auto& k : cc.component_indexes) {

				for(auto& ri : ob.indexed_objects) {
					if(ri.property_name == k.property_name) {
						k.object_type = ri.type_name;
						ri.is_covered_by_composite_key = true;
						k.multiplicity = ri.multiplicity;
					}
				}

				if(k.object_type.length() == 0) {
					err.add(row_col_pair{ 0, 0}, 1006, std::string("Indexed link ") + k.property_name + " in composite key " + cc.name +
						" in relationship " + ob.name + " does not refer to a link in the relationship.");
					error_to_file(dll_header_name);
					std::cout << err.accumulated;
					return -1;
				}

				k.bit_position = bits_so_far;
				if(k.property_name == ob.primary_key.property_name)
					cc.involves_primary_key = true;

				if(ob.is_expandable) {
					k.number_of_bits = 32;
					bits_so_far += 32;
				} else {
					k.number_of_bits = 0;
					for(auto sz = ob.size; sz != 0; sz = sz >> 1) {
						++k.number_of_bits;
						bits_so_far += k.multiplicity;
					}
				}
			}

			cc.total_bytes = (bits_so_far + 7) / 8;
			if(cc.total_bytes > 8 &&
				std::find(byte_sizes_need_hash.begin(), byte_sizes_need_hash.end(), cc.total_bytes) == byte_sizes_need_hash.end()) {
				byte_sizes_need_hash.push_back(cc.total_bytes);
			}
		}
	}

	// make prepared queries

	for(auto& q : parsed_file.unprepared_queries) {
		parsed_file.prepared_queries.push_back(make_prepared_definition(parsed_file, q, err));
	}
	if(err.accumulated.length() > 0) {
		error_to_file(dll_header_name);
		std::cout << err.accumulated;
		return -1;
	}

	// compose contents of generated file
	std::string output;
	std::string header_output;
	std::string lua_manager;

	lua_manager += "-- GENERATED FILE: DO NOT EDIT --\n";


	output += "//\n";
	output += "// This file was automatically generated from: " + std::string(argv[3]) + "\n";
	output += "// EDIT AT YOUR OWN RISK; all changes will be lost upon regeneration\n";
	output += "// NOT SUITABLE FOR USE IN CRITICAL SOFTWARE WHERE LIVES OR LIVELIHOODS DEPEND ON THE CORRECT OPERATION\n";
	output += "//\n";
	output += "\n";
	output += "#define DCON_LUADLL_EXPORTS\n";
	output += "#include \"" + base_include_name + "\"\n";
	if(parsed_file.load_save_routines.size() > 0) {
		output += "#include <fstream>\n";
		output += "#include <filesystem>\n";
		output += "#include <iostream>\n";
	}


	header_output += "#pragma once\n";
	header_output += "\n";
	header_output += "//\n";
	header_output += "// This file was automatically generated from: " + std::string(argv[3]) + "\n";
	header_output += "// EDIT AT YOUR OWN RISK; all changes will be lost upon regeneration\n";
	header_output += "// NOT SUITABLE FOR USE IN CRITICAL SOFTWARE WHERE LIVES OR LIVELIHOODS DEPEND ON THE CORRECT OPERATION\n";
	header_output += "//\n";
	header_output += "\n";
	header_output += "#include <stdint.h>\n";
	header_output += "using lua_reference_type = int32_t;\n";
	header_output += "#include \"" + base_include_name + "\"\n";
	header_output += "#ifdef _WIN32\n";
	header_output += "#define DCON_LUADLL_API __declspec(dllexport)\n";
	header_output += "#else\n";
	header_output += "#define DCON_LUADLL_API __attribute__((visibility(\"default\")))\n";
	header_output += "#endif\n";


	//extern "C" DCON_LUADLL_API void

	//open new namespace
	header_output += "\n";
	// header_output += parsed_file.namspace + "::data_container game_state;\n";
	header_output += "\n";

	output += "\n";
	// output += "DCON_LUADLL_API " + parsed_file.namspace + "::data_container* state_ffi_ptr;\n";
	output += "void (*release_object_function)(int32_t) = nullptr;\n";
	output += "\n";

	header_output += "extern \"C\" {\n";

	header_output += "DCON_LUADLL_API void " + project_prefix + "set_release_object_function(void (*fn)(int32_t));\n";
	output += "void " + project_prefix + "set_release_object_function(void (*fn)(int32_t)) {\n";
	output += "\trelease_object_function = fn;\n";
	output += "}\n";

	for(auto& ob : parsed_file.relationship_objects) {
		made_types.insert(ob.name + "_id");
	}

	std::string lua_ids_collection = "";

	for(auto& mi : parsed_file.extra_ids) {
		made_types.insert(mi.name);
		lua_ids_collection += "---@class (exact)" + lua_id(mi.name) + " : table\n";
		lua_ids_collection += "---@field _is_" + mi.name + "_id true\n\n";
	}


	for(auto& ob : parsed_file.relationship_objects) {

		// std::string lua_meta = 	"--    GENERATED FILE: DO NOT EDIT     --\n";
		// lua_meta += 		"-- PROVIDES TYPING FOR GENERATED CODE --\n";
		// lua_meta += 		"--            DO NOT REQUIRE          --\n";

		std::string lua_cdef = 	"-- GENERATED FILE: DO NOT EDIT --\n";
		lua_cdef +=		"--   PROVIDES FFI DECLARATIONS --\n";


		// lua_meta += "--@meta\n";
		lua_cdef += "local ffi = require(\"ffi\")\n\n";

		// strongly typed weak id
		lua_ids_collection += "---@class (exact)" + lua_id(ob.name + "_id") + " : table\n";
		lua_ids_collection += "---@field _is_" + ob.name + "_id true\n\n";

		std::string lua_namespace = ob.name;
		for (size_t i = 0; i < lua_namespace.length(); i++) {
			lua_namespace[i] = std::toupper(lua_namespace[i]);
		}

		std::string lua_cdef_wrapper = "";
		lua_cdef_wrapper += lua_namespace + " = {}\n";

		lua_cdef += "ffi.cdef[[\n";

		auto gen_call_information = [&](std::string property, array_access access_type, std::vector<arg_information> in, arg_information out) {
			function_call_information call {
				.access_type = access_type,
				.project_prefix = project_prefix,
				.accessed_object = ob.name,
				.accessed_property = property,
				.in = in,
				.out = out
			};
			return call;
		};

		auto append_call = [&](function_call_information call) {
			std::string head = generate_head(call);
			std::string body = generate_body(parsed_file, call);
			header_output += "DCON_LUADLL_API " + head + ";\n";
			output += head + body;

			return head;
		};


		auto append_lua = [&](function_call_information call) {
			std::string lua_args = "";
			lua_cdef += generate_head(call) + ";\n";
			for (auto item : call.in) {
				lua_args += item.name;
				lua_cdef_wrapper += "---@param ";
				lua_cdef_wrapper += item.name;
				lua_cdef_wrapper += " ";
				if (item.meta_type == meta_information::id) {
					lua_cdef_wrapper += lua_id(item.type.c_type);
				} else if (item.meta_type == meta_information::value) {
					lua_cdef_wrapper += item.type.lua_type;
				}
				lua_cdef_wrapper += "\n";
				lua_args += ", ";
			}
			if (call.out.meta_type != meta_information::empty) {
				if (call.out.meta_type == meta_information::id) {
					lua_cdef_wrapper += "---@return " + lua_id(call.out.type.c_type) + "\n";
				} else {
					lua_cdef_wrapper += "---@return " + call.out.type.lua_type + "\n";
				}
			}

			if (lua_args.length() > 0) {
				lua_args.pop_back();
				lua_args.pop_back();
			}
			std::string property = call.accessed_property;
			// replace for vector pools
			if (call.access_type == array_access::get_call) {
				property = "get_" + property;
			} else if (call.access_type == array_access::set_call) {
				property = "set_" + property;
			} else if (call.access_type == array_access::resize_call) {
				property = "size_" + property;
			} else if (call.access_type == array_access::size_call) {
				property = "resize_" + property;
			}
			lua_cdef_wrapper += "function " + lua_namespace + "." + property + "(";
			lua_cdef_wrapper += lua_args;
			lua_cdef_wrapper += ")\n";
			lua_cdef_wrapper += "\treturn ffi.C." + access_property_name(call) + "(" + lua_args + ")\n";
			lua_cdef_wrapper += "end\n";
		};

		auto append = [&](function_call_information declaration) {
			append_call(declaration);
			if (declaration.out.meta_type == meta_information::value_pointer) return;
			for (auto& item : declaration.in) {
				if (item.meta_type == meta_information::value_pointer) {
					return;
				}
			}
			append_lua(declaration);
		};

		arg_information id_in = {
			.meta_type = meta_information::id,
			.type = normalize_type(convert_to_id(ob.name), made_types),
			.name = "id",
		};

		auto gen_value = [&](std::string name, std::string base) {
			arg_information out = {
				.meta_type = meta_information::value,
				.type = normalize_type(base, made_types),
				.name = name
			};
			return out;
		};

		arg_information void_type = {
			.meta_type = meta_information::empty,
			.type = {},
			.name = "???"
		};

		auto size_type = gen_value("value", "uint32_t");
		auto int_type = gen_value("value", "int32_t");
		auto bool_type = gen_value("value", "bool");

		// append_id_to_value("is_valid", "bool", "boolean");
		append(gen_call_information("is_valid", array_access::function_call, {id_in}, bool_type));
		append(gen_call_information("size", array_access::function_call, {}, size_type));
		append(gen_call_information("resize", array_access::function_call, {size_type}, void_type));

		for(auto& prop : ob.properties) {
			auto is_bool = prop.type == property_type::array_bitfield || prop.type == property_type::bitfield;
			arg_information value = normalize_argument("value", is_bool, prop.data_type);
			auto index = normalize_argument("index", false, prop.array_index_type);

			if(prop.type == property_type::array_bitfield || prop.type == property_type::array_vectorizable || prop.type == property_type::array_other) {
				if((prop.hook_get || !prop.is_derived) && value.type.normalized != lua_type_match::lua_object && (index.meta_type != meta_information::value_pointer)) {
					append(
						gen_call_information(
							"get_" + prop.name,
							array_access::function_call,
							{
								id_in,
								index
							},
							value
						)
					);
					if (value.type.normalized != lua_type_match::opaque) append(
						gen_call_information(
							"set_" + prop.name,
							array_access::function_call,
							{
								id_in,
								index,
								value
							},
							void_type
						)
					);
				}

				if(!prop.is_derived) {
					append(gen_call_information("get_" + prop.name + "_size", array_access::function_call, {}, size_type));
					append(gen_call_information("resize_" + prop.name, array_access::function_call, {size_type}, void_type));
				}
			} else if(prop.type == property_type::special_vector) {
				if((prop.hook_get || !prop.is_derived) && value.type.normalized != lua_type_match::lua_object && (index.meta_type != meta_information::value_pointer)) {
					append(
						gen_call_information(
							prop.name,
							array_access::get_call,
							{
								id_in,
								size_type
							},
							value
						)
					);
					append(
						gen_call_information(
							prop.name,
							array_access::size_call,
							{
								id_in
							},
							size_type
						)
					);
					append(
						gen_call_information(
							prop.name,
							array_access::resize_call,
							{
								id_in, size_type
							},
							void_type
						)
					);
					if (value.type.normalized != lua_type_match::opaque) append(
						gen_call_information(
							prop.name,
							array_access::set_call,
							{
								id_in,
								size_type,
								value
							},
							void_type
						)
					);
				}
			} else {
				if((prop.hook_get || !prop.is_derived) && value.type.normalized != lua_type_match::lua_object) {
					append(
						gen_call_information(
							"get_" + prop.name,
							array_access::function_call,
							{
								id_in,
							},
							value
						)
					);
					if (value.type.normalized != lua_type_match::opaque) append(
						gen_call_information(
							"set_" + prop.name,
							array_access::function_call,
							{
								id_in,
								value
							},
							void_type
						)
					);
				}
			}
		} // end: loop over properties

		for(auto& indexed : ob.indexed_objects) {
			arg_information value {
				.meta_type = meta_information::id,
				.type = normalize_type(convert_to_id(indexed.type_name), made_types),
				.name = "linked_id",
			};
			if(indexed.index == index_type::at_most_one && ob.primary_key == indexed) {
				append(gen_call_information(
					"get_" + indexed.property_name,
					array_access::function_call,
					{id_in},
					value
				));
				append(gen_call_information(
					"set_" + indexed.property_name,
					array_access::function_call,
					{id_in, value},
					void_type
				));
				append(gen_call_information(
					"try_set_" + indexed.property_name,
					array_access::function_call,
					{id_in, value},
					void_type
				));
			} else { // if(indexed.index == index_type::at_most_one ||  index_type::many || unindexed
				if(indexed.multiplicity == 1) {
					append(gen_call_information(
						"get_" + indexed.property_name,
						array_access::function_call,
						{id_in},
						value
					));
					append(gen_call_information(
						"set_" + indexed.property_name,
						array_access::function_call,
						{id_in, value},
						void_type
					));
					append(gen_call_information(
						"try_set_" + indexed.property_name,
						array_access::function_call,
						{id_in, value},
						void_type
					));
				} else {
					append(gen_call_information(
						"get_" + indexed.property_name,
						array_access::function_call,
						{id_in, int_type},
						value
					));
					append(gen_call_information(
						"set_" + indexed.property_name,
						array_access::function_call,
						{id_in, int_type, value},
						void_type
					));
					append(gen_call_information(
						"try_set_" + indexed.property_name,
						array_access::function_call,
						{id_in, int_type, value},
						void_type
					));
				}
			}
		} // end: loop over indexed objects

		for(auto& involved_in : ob.relationships_involved_in) {
			arg_information involved_relation {
				.meta_type = meta_information::id,
				.type = normalize_type(convert_to_id(involved_in.relation_name), made_types),
				.name = "relation",
			};
			if(involved_in.linked_as->index == index_type::at_most_one) {
				append(gen_call_information(
					"get_" + involved_in.relation_name + "_as_" + involved_in.linked_as->property_name,
					array_access::function_call,
					{id_in},
					involved_relation
				));
				bool is_only_of_type = true;
				for(auto& ir : involved_in.rel_ptr->indexed_objects) {
					if(ir.type_name == ob.name && ir.property_name != involved_in.linked_as->property_name)
						is_only_of_type = false;
				}
				if(is_only_of_type) {
					append(gen_call_information(
						"get_" + involved_in.relation_name,
						array_access::function_call,
						{id_in},
						involved_relation
					));
				} // end: is only of type

			} else if(involved_in.linked_as->index == index_type::many) {
				if(involved_in.linked_as->ltype == list_type::array || involved_in.linked_as->ltype == list_type::std_vector) {
					auto access = project_prefix + ob.name + "_get_range_" + involved_in.relation_name + "_as_" + involved_in.linked_as->property_name;
					header_output += "DCON_LUADLL_API int32_t " + access + "(int32_t i); \n";
					output += "int32_t " + project_prefix + access + "(int32_t i) { \n";
					output += "\tauto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(i)};\n";
					output += "\tauto rng = " + game_state + ob.name + "_get_" + involved_in.relation_name + "_as_" + involved_in.linked_as->property_name + "(index);\n";
					output += "\treturn int32_t(rng.end() - rng.begin());\n";
					output += "}\n";
					lua_cdef_wrapper += "---@param id " + lua_id(ob.name + "_id") + "\n";
					lua_cdef_wrapper += "---@return number\n";
					lua_cdef_wrapper += "function " + lua_namespace + ".get_range_length_" + involved_in.relation_name + "(id)\n";
					lua_cdef_wrapper += "\treturn ffi.C." + access + "(id)\n";
					lua_cdef_wrapper += "end\n";

					access = project_prefix + ob.name + "_get_index_" + involved_in.relation_name + "_as_" + involved_in.linked_as->property_name;
					header_output += "DCON_LUADLL_API int32_t " + access + "(int32_t i, int32_t subindex); \n";
					output += "int32_t " + access + "(int32_t i, int32_t subindex) { \n";
					output += "\tauto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(i)};\n";
					output += "\tauto rng = " + game_state + ob.name + "_get_" + involved_in.relation_name + "_as_" + involved_in.linked_as->property_name + "(index);\n";
					output += "\treturn rng.begin()[subindex].id.index();\n";
					output += "}\n";
					lua_cdef_wrapper += "---@param id " + lua_id(ob.name + "_id") + "\n";
					lua_cdef_wrapper += "---@param index number\n";
					lua_cdef_wrapper += "---@return " + lua_id(involved_in.relation_name + "_id") + "\n";
					lua_cdef_wrapper += "function " + lua_namespace + ".get_item_from_range_" + involved_in.relation_name + "(id, index)\n";
					lua_cdef_wrapper += "\treturn ffi.C." + access + "(id, index)\n";
					lua_cdef_wrapper += "end\n";
				}


				bool is_only_of_type = true;
				for(auto& ir : involved_in.rel_ptr->indexed_objects) {
					if(ir.type_name == ob.name && ir.property_name != involved_in.linked_as->property_name)
						is_only_of_type = false;
				}
				if(is_only_of_type) {
					header_output += "DCON_LUADLL_API int32_t " + project_prefix + ob.name + "_get_range_" + involved_in.relation_name + "(int32_t i); \n";
					output += "int32_t " + project_prefix + ob.name + "_get_range_" + involved_in.relation_name  + "(int32_t i) { \n";
					output += "\tauto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(i)};\n";
					output += "\tauto rng = "+game_state + ob.name + "_get_" + involved_in.relation_name + "_as_" + involved_in.linked_as->property_name + "(index);\n";
					output += "\treturn int32_t(rng.end() - rng.begin());\n";
					output += "}\n";

					header_output += "DCON_LUADLL_API int32_t " + project_prefix + ob.name + "_get_index_" + involved_in.relation_name + "(int32_t i, int32_t subindex); \n";
					output += "int32_t " + project_prefix + ob.name + "_get_index_" + involved_in.relation_name + "(int32_t i, int32_t subindex) { \n";
					output += "\tauto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(i)};\n";
					output += "\tauto rng = "+game_state + ob.name + "_get_" + involved_in.relation_name + "_as_" + involved_in.linked_as->property_name + "(index);\n";
					output += "\treturn rng.begin()[subindex].id.index();\n";
					output += "}\n";
				}
			}
		} // end: loop over relationships involved in

		output += "\n";

		const std::string id_name = ob.name + "_id";
		auto make_pop_back_delete = [&]() {
			header_output += "DCON_LUADLL_API void " + project_prefix + "pop_back_" + ob.name + "(); \n";
			output += "void " + project_prefix + "pop_back_" + ob.name + "() { \n";
			output += "\tif("+game_state + ob.name + "_size() > 0) {\n";
			output += "\t\tauto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t("+game_state + ob.name + "_size() - 1)};\n";
			for(auto& p : ob.properties) {
				if(p.data_type == "lua_reference_type") {
					if(p.type == property_type::array_vectorizable || p.type == property_type::array_other) {
						output += "\t\tfor(auto i = "+game_state + ob.name + "_get_" + p.name + "_size(); i-->0; ) {\n";
						if(made_types.count(p.array_index_type) > 0) {
							output += "\t\t\tif(auto result = "+game_state + ob.name + "_get_" + p.name + "(index, " + parsed_file.namspace + "::" + p.array_index_type + "{" + parsed_file.namspace + "::" + p.array_index_type + "::value_base_t(i)}); result != 0) release_object_function(result);\n";
						} else {
							output += "\t\t\tif(auto result = "+game_state + ob.name + "_get_" + p.name + "(index, " + p.array_index_type + "(i)); result != 0) release_object_function(result);\n";
						}
						output += "\t\t}\n";
					} else if(p.type == property_type::special_vector) {
						output += "\t\t" + project_prefix + ob.name + "_resize_" + p.name + "(index.index(), 0);\n";
					} else {
						output += "\t\tif(auto result = "+game_state + ob.name + "_get_" + p.name + "(index); result != 0) release_object_function(result);\n";
					}
				}
			}
			output += "\t\t"+game_state+"pop_back_" + ob.name + "();\n";
			output += "\t}\n";
			output += "}\n";
		};
		auto make_simple_create = [&]() {
			header_output += "DCON_LUADLL_API int32_t " + project_prefix + "create_" + ob.name + "(); \n";
			output += "int32_t " + project_prefix + "create_" + ob.name + "() { \n";
			output += "\tauto result = "+game_state+"create_" + ob.name + "();\n";
			output += "\treturn result.index();\n";
			output += "}\n";
		};
		auto make_delete = [&]() {
			header_output += "DCON_LUADLL_API void " + project_prefix + "delete_" + ob.name + "(int32_t j); \n";
			output += "void " + project_prefix + "delete_" + ob.name + "(int32_t j) { \n";
			output += "\tauto index = " + parsed_file.namspace + "::" + ob.name + "_id{" + parsed_file.namspace + "::" + ob.name + "_id::value_base_t(j)};\n";
			for(auto& p : ob.properties) {
				if(p.data_type == "lua_reference_type") {
					if(p.type == property_type::array_vectorizable || p.type == property_type::array_other) {
						output += "\t\tfor(auto i = "+game_state + ob.name + "_get_" + p.name + "_size(); i-->0; ) {\n";
						if(made_types.count(p.array_index_type) > 0) {
							output += "\t\t\tif(auto result = "+game_state + ob.name + "_get_" + p.name + "(index, " + parsed_file.namspace + "::" + p.array_index_type + "{" + parsed_file.namspace + "::" + p.array_index_type + "::value_base_t(i)}); result != 0) release_object_function(result);\n";
						} else {
							output += "\t\t\tif(auto result = "+game_state + ob.name + "_get_" + p.name + "(index, " + p.array_index_type + "(i)); result != 0) release_object_function(result);\n";
						}
						output += "\t\t}\n";
					} else if(p.type == property_type::special_vector) {
						output += "\t\t" + project_prefix + ob.name + "_resize_" + p.name + "(j, 0);\n";
					} else {
						output += "\t\tif(auto result = "+game_state + ob.name + "_get_" + p.name + "(index); result != 0) release_object_function(result);\n";
					}
				}
			}
			output += "\t"+game_state+"delete_" + ob.name + "(index);\n";
			output += "}\n";
		};
		auto make_relation_create = [&]() {
			std::string params;
			std::string pargs;
			int32_t pcount = 1;
			for(auto& i : ob.indexed_objects) {
				if(params.length() != 0) {
					params += ", ";
					pargs += ", ";
				}
				if(i.multiplicity == 1) {
					params += parsed_file.namspace + "::" + i.type_name + "_id{" + parsed_file.namspace + "::" + i.type_name + "_id::value_base_t(p" + std::to_string(pcount) + ")}";
					pargs += "int32_t p" + std::to_string(pcount);
					pcount++;
				} else {
					params += parsed_file.namspace + "::" + i.type_name + "_id{" + parsed_file.namspace + "::" + i.type_name + "_id::value_base_t(p" + std::to_string(pcount) + ")}";
					pargs += "int32_t p" + std::to_string(pcount);
					pcount++;

					for(int32_t j = 1; j < i.multiplicity; ++j) {
						params += ", " + parsed_file.namspace + "::" + i.type_name + "_id{" + parsed_file.namspace + "::" + i.type_name + "_id::value_base_t(p" + std::to_string(pcount) + ")}";
						pargs += ", int32_t p" + std::to_string(pcount);
						pcount++;
					}
				}
			}

			header_output += "DCON_LUADLL_API int32_t " + project_prefix + "try_create_" + ob.name + "(" + pargs + "); \n";
			output += "int32_t " + project_prefix + "try_create_" + ob.name + "(" + pargs + ") { \n";
			output += "\tauto result = "+game_state+"try_create_" + ob.name + "(" + params + ");\n";
			output += "\treturn result.index();\n";
			output += "}\n";

			header_output += "DCON_LUADLL_API int32_t " + project_prefix + "force_create_" + ob.name + "(" + pargs + "); \n";
			output += "int32_t " + project_prefix + "force_create_" + ob.name + "(" + pargs + ") { \n";
			output += "\tauto result = "+game_state+"force_create_" + ob.name + "(" + params + ");\n";
			output += "\treturn result.index();\n";
			output += "}\n";
		};

		if(!ob.is_relationship) {
			if(ob.store_type == storage_type::contiguous || ob.store_type == storage_type::compactable) {
				make_pop_back_delete();
				make_simple_create();

				if(ob.store_type == storage_type::compactable) {
					make_delete();
				}

			} else if(ob.store_type == storage_type::erasable) {
				make_delete();
				make_simple_create();
			}
		} else if(ob.primary_key.points_to) { // primary key relationship
			make_delete();
			make_relation_create();
		} else { // non pk relationship
			if(ob.store_type == storage_type::contiguous || ob.store_type == storage_type::compactable) {
				make_pop_back_delete();
				make_relation_create();

				if(ob.store_type == storage_type::compactable) {
					make_delete();
				}
			} else if(ob.store_type == storage_type::erasable) {
				make_delete();
				make_relation_create();
			}
		} // end case relationship no primary key

		for(auto& cc : ob.composite_indexes) {
			std::string params;
			std::string pargs;
			int32_t pcount = 1;
			for(auto& k : cc.component_indexes) {
				if(params.length() > 0) {
					params += ", ";
					pargs += ", ";
				}
				if(k.multiplicity == 1) {
					params += parsed_file.namspace + "::" + k.object_type + "_id{" + parsed_file.namspace + "::" + k.object_type + "_id::value_base_t(p" + std::to_string(pcount) + ")}";
					pargs += "int32_t p" + std::to_string(pcount);
					pcount++;
				} else {
					params += parsed_file.namspace + "::" + k.object_type + "_id{" + parsed_file.namspace + "::" + k.object_type + "_id::value_base_t(p" + std::to_string(pcount) + ")}";
					pargs += "int32_t p" + std::to_string(pcount);
					pcount++;

					for(int32_t i = 1; i < k.multiplicity; ++i) {
						params += ", " + parsed_file.namspace + "::" + k.object_type + "_id{" + parsed_file.namspace + "::" + k.object_type + "_id::value_base_t(p" + std::to_string(pcount) + ")}";
						pargs += ", int32_t p" + std::to_string(pcount);
						pcount++;
					}
				}
			}

			header_output += "DCON_LUADLL_API int32_t " + project_prefix + "get_" + ob.name + "_by_" + cc.name + "(" + pargs +"); \n";
			output += "int32_t " + project_prefix + "get_" + ob.name + "_by_" + cc.name + "(" + pargs + ") { \n";
			output += "\tauto result = "+game_state+"get_" + ob.name + "_by_" + cc.name + "(" + params + ");\n";
			output += "\treturn result.index();\n";
			output += "}\n";

		}

		lua_cdef += "]]\n";

		{
			std::fstream lua_file;
			std::filesystem::create_directory(lua_folder);
			lua_file.open(lua_folder + "/" + ob.name + ".lua", std::ios::out);
			if(lua_file.is_open()) {
				lua_file << lua_cdef << lua_cdef_wrapper;
				lua_file.close();
			} else {
				std::abort();
			}
		}
	}

	output += "\n";
	//reset function

	header_output += "DCON_LUADLL_API int32_t " + project_prefix + "reset(); \n";
	output += "int32_t " + project_prefix + "reset() { \n";
	output += "\t"+game_state+"reset();\n";
	output += "\treturn 0;\n";
	output += "}\n";


	for(auto& rt : parsed_file.load_save_routines) {
		header_output += "DCON_LUADLL_API void " + project_prefix + rt.name + "_write_file(char const* name); \n";
		output += "void " + project_prefix + rt.name + "_write_file(char const* name) { \n";
		output += "\tstd::ofstream file_out(name, std::ios::binary);\n";
		output += "\t"+ parsed_file.namspace + "::load_record selection = "+game_state+"make_serialize_record_" + rt.name + "();\n";
		output += "\tauto sz = "+game_state+"serialize_size(selection);\n";
		output += "\tstd::byte* temp_buffer = new std::byte[sz];\n";
		output += "\tauto ptr = temp_buffer;\n";
		output += "\t"+game_state+"serialize(ptr, selection); \n";
		output += "\tfile_out.write((char*)temp_buffer, sz);\n";
		output += "\tdelete[] temp_buffer;\n";
		output += "}\n";

		header_output += "DCON_LUADLL_API void " + project_prefix + rt.name + "_read_file(char const* name); \n";
		output += "void " + project_prefix + rt.name + "_read_file(char const* name) { \n";
		output += "\tstd::ifstream file_in(name, std::ios::binary);\n";
		output += "\tfile_in.unsetf(std::ios::skipws);\n";
		output += "\tfile_in.seekg(0, std::ios::end);\n";
		output += "\tauto sz = file_in.tellg();\n";
		output += "\tfile_in.seekg(0, std::ios::beg);\n";
		output += "\tstd::vector<unsigned char> vec;\n";
		output += "\tvec.reserve(sz);\n";
		output += "\tvec.insert(vec.begin(), std::istream_iterator<unsigned char>(file_in),  std::istream_iterator<unsigned char>());\n";
		output += "\tstd::byte const* ptr = (std::byte const*)(vec.data());\n";
		output += "\t" + parsed_file.namspace + "::load_record loaded;\n";
		output += "\t" + parsed_file.namspace + "::load_record selection = "+game_state+"make_serialize_record_" + rt.name + "();\n";
		output += "\t"+game_state+"deserialize(ptr, ptr + sz, loaded, selection); \n";
		output += "}\n";
	}

	header_output += "}\n"; // close extern C

	//newline at end of file
	output += "\n";

	{
		std::fstream fileout;
		fileout.open(dll_source_name, std::ios::out);
		if(fileout.is_open()) {
			fileout << output;
			fileout.close();
		} else {
			std::abort();
		}
	}
	{
		std::fstream fileout;
		fileout.open(dll_header_name, std::ios::out);
		if(fileout.is_open()) {
			fileout << header_output;
			fileout.close();
		} else {
			std::abort();
		}
	}

	{
		std::fstream fileout;
		fileout.open(lua_folder + "/_ids.lua", std::ios::out);
		if(fileout.is_open()) {
			fileout << lua_ids_collection;
			fileout.close();
		} else {
			std::abort();
		}
	}
}
