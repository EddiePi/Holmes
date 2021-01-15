#include <fstream>
#include <string>
#include <iostream>
#include <sstream>
#include <climits>
#include "conf.hpp"

using std::string;
using std::ifstream;
using std::istringstream;
using std::cout;
using std::cerr;
using std::endl;

conf::conf(string conf_path)
{
	ifstream conf_file(conf_path);
	if (!conf_file.is_open())
	{
		cerr << "error in opening conf file" << endl;
		return;
	}
	else {
		string line;
		while(!conf_file.eof())
		{
			std::getline(conf_file, line);
			istringstream iss(line);
			string key;
			string value;
			iss >> key;
			iss >> value;
			this->confs[key] = value;
		}
		conf_file.close();
	}
}

string conf::get_string(string name)
{
	auto it = this->confs.find(name);
	if (it != this->confs.end())
	{
		return it->second;
	}
	return "";
}


int conf::get_int(string name)
{
	string value_str;
	value_str = this->get_string(name);
	if (!value_str.empty())
	{
		int value;
		value = std::stoi(value_str);
		return value;
	}
	return INT_MIN;
}

uint64_t conf::get_int64(string name)
{
	string value_str;
	value_str = this->get_string(name);
	if (!value_str.empty())
	{
		uint64_t value;
		value = strtoull(value_str.c_str(), NULL, 10);
		return value;
	}
	return INT_MIN;
}

bool conf::get_bool(string name)
{
	string value_str;
	value_str = this->get_string(name);
	if (!value_str.empty())
	{
		if (value_str == "true")
		{
			return 1;
		}
		else if (value_str == "false")
		{
			return 0;
		}
	}
	return 0;
}
