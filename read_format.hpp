#ifndef READ_FORMAT_HPP_
#define READ_FORMAT_HPP_

#include <inttypes.h>

struct read_format {
	uint64_t nr;
	struct {
		uint64_t value;
		uint64_t id;
	}values[];
};

#endif // READ_FORMAT_HPP