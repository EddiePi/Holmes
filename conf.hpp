#ifndef CONF_HPP_
#define CONF_HPP_
#include <string>
#include <map>

using std::map;
using std::string;

class conf {
	public:
		conf(string conf_path);
		int get_int(string name);
		uint64_t get_int64(string name);
		string get_string(string name);
		bool get_bool(string name);

	private:
		map<string, string> confs;
};

#endif // CONF_HPP_
