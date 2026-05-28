#pragma once

#include "gen-cpp2/eugraph_types.h"

#include <string>

namespace eugraph::thrift_fmt {

std::string formatResultValue(const thrift::ResultValue& rv);
std::string propertyTypeToString(thrift::PropertyType t);
thrift::PropertyType parsePropertyType(const std::string& type_str);

} // namespace eugraph::thrift_fmt
