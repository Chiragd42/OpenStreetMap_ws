#pragma once
#include "spatial_index.hpp"
#include <string>
#include <iosfwd>
namespace sheet1 {
std::string reverse_json(const DataStore&,const SpatialIndex&,Coordinate);
std::string geometry_json(const DataStore&,const SpatialIndex&,Coordinate,Coordinate,int,int zoom=19);
void append_regions_json(std::ostream&,const DataStore&,std::uint64_t,std::uint32_t);
}
