#include "spatial_json.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace sheet1 {
namespace {
void quote(std::ostream& out,std::string_view s) {
    out<<'"';for(char raw:s) {const auto c=static_cast<unsigned char>(raw);if(c=='"'||c=='\\') out<<'\\'<<c;else if(c<32)out<<"\\u00"<<std::hex<<std::setw(2)<<std::setfill('0')<<static_cast<int>(c)<<std::dec<<std::setfill(' ');else out<<c;}out<<'"';
}
std::string text(const DataStore& d,StringId id) {return id==kInvalidStringId?"":d.strings.resolve(id);}
void coords(std::ostream& o,Coordinate p) {o<<'['<<p.longitude_e7*1e-7<<','<<p.latitude_e7*1e-7<<']';}
void region(std::ostream& o,const DataStore& d,std::size_t i) {
    const auto& a=d.administrative_areas[i];o<<"{\"id\":";quote(o,format_osm_id(a.id));o<<",\"name\":";quote(o,text(d,a.name));o<<",\"admin_level\":"<<a.admin_level<<",\"postcode\":";quote(o,text(d,a.postcode));o<<'}';
}
void geometry(std::ostream& o,const DataStore& d,int kind,std::size_t i) {
    o<<"{\"type\":";
    if(kind==1) {
        quote(o,"LineString");o<<",\"coordinates\":[";const auto& s=d.streets[i];
        for(std::size_t j=0;j<s.points_count;++j) {if(j)o<<',';coords(o,d.street_points[s.points_begin+j]);}o<<"]}";
    } else {
        quote(o,"MultiPolygon");o<<",\"coordinates\":[";const auto& a=d.administrative_areas[i];bool first=true;
        for(std::size_t j=0;j<a.rings_count;++j) {
            const auto& r=d.administrative_rings[a.rings_begin+j];
            if(!r.is_inner) {if(!first)o<<"],";o<<'[';first=false;} else o<<',';
            o<<'[';for(std::size_t k=0;k<r.points_count;++k) {if(k)o<<',';coords(o,d.administrative_points[r.points_begin+k]);}o<<']';
        }
        if(!first)o<<']';o<<"]}";
    }
}
void object(std::ostream& o,const DataStore& d,const SpatialIndex& index,const NearestObject& hit) {
    OsmId id;StringId name;std::uint64_t begin;std::uint32_t count;
    std::string street,number,zip,city,state,country;
    const int kind=hit.kind==NearestObject::Kind::building?0:hit.kind==NearestObject::Kind::street?1:2;
    if(kind==0) {const auto& b=d.buildings[hit.index];id=b.id;name=b.name;begin=b.regions_begin;count=b.regions_count;street=text(d,b.street);number=text(d,b.house_number);zip=text(d,b.postcode);city=text(d,b.city);state=text(d,b.state);country=text(d,b.country);}
    else if(kind==1) {const auto& s=d.streets[hit.index];id=s.id;name=s.name;begin=s.regions_begin;count=s.regions_count;street=text(d,s.name);}
    else {const auto& a=d.administrative_areas[hit.index];id=a.id;name=a.name;begin=a.regions_begin;count=a.regions_count;}
    // Address context at the closest point avoids choosing the wrong end of a cross-border way.
    const auto context=index.regions_at(hit.point);int city_level=-1;
    for(auto i:context) {const auto& a=d.administrative_areas[i];
        if(zip.empty() && a.postcode!=kInvalidStringId)zip=text(d,a.postcode);
        if(a.admin_level==2 && country.empty())country=text(d,a.name);
        if(a.admin_level==4 && state.empty())state=text(d,a.name);
        if(a.admin_level>=6 && a.admin_level<=8 && a.admin_level>city_level) {city_level=a.admin_level;if(kind!=0||d.buildings[hit.index].city==kInvalidStringId)city=text(d,a.name);}
    }
    o<<"{\"id\":";quote(o,format_osm_id(id));o<<",\"kind\":";quote(o,kind==0?"building":kind==1?"street":"area");o<<",\"name\":";quote(o,text(d,name));
    o<<",\"distance_m\":"<<hit.distance_metres<<",\"lat\":"<<hit.point.latitude_e7*1e-7<<",\"lon\":"<<hit.point.longitude_e7*1e-7;
    for(const auto& field:{std::pair{"street",street},{"house_number",number},{"postcode",zip},{"city",city},{"state",state},{"country",country}}) {o<<",\""<<field.first<<"\":";quote(o,field.second);}
    std::string address;for(const auto& s:{street,number,zip,city,state,country}) if(!s.empty()) {if(!address.empty()) address+=", ";address+=s;}
    o<<",\"full_address\":";quote(o,address);o<<",\"regions\":";append_regions_json(o,d,begin,count);
    o<<",\"address_complete\":"<<(!street.empty()&&!number.empty()&&!zip.empty()&&!city.empty()&&!country.empty()?"true":"false");
    o<<",\"geometry\":";if(kind==0){o<<"{\"type\":\"Point\",\"coordinates\":";coords(o,hit.point);o<<'}';}else geometry(o,d,kind,hit.index);o<<'}';
}
}
void append_regions_json(std::ostream& o,const DataStore& d,std::uint64_t begin,std::uint32_t count) {
    o<<'[';for(std::size_t j=0;j<count;++j){if(j)o<<',';region(o,d,d.region_memberships[begin+j]);}o<<']';
}
std::string reverse_json(const DataStore& d,const SpatialIndex& index,Coordinate p) {
    std::ostringstream o;o<<std::fixed<<std::setprecision(7);auto hits=index.reverse(p);
    o<<"{\"nearest\":";if(hits.empty())o<<"null";else object(o,d,index,hits.front());
    o<<",\"results\":[";for(std::size_t i=0;i<hits.size();++i){if(i)o<<',';object(o,d,index,hits[i]);}o<<"],\"buildings\":[";
    auto buildings=index.nearest_buildings(p);for(std::size_t i=0;i<buildings.size();++i){if(i)o<<',';object(o,d,index,buildings[i]);}
    o<<"],\"regions\":[";auto regions=index.regions_at(p);for(std::size_t i=0;i<regions.size();++i){if(i)o<<',';region(o,d,regions[i]);}o<<"]}";return o.str();
}
std::string geometry_json(const DataStore& d,const SpatialIndex& index,Coordinate minimum,Coordinate maximum,int kind) {
    std::ostringstream o;o<<std::fixed<<std::setprecision(7)<<"{\"type\":\"FeatureCollection\",\"features\":[";
    auto ids=index.viewport(minimum,maximum,kind,kind==1?1000:50);
    for(std::size_t k=0;k<ids.size();++k) {auto i=ids[k];if(k)o<<',';o<<"{\"type\":\"Feature\",\"properties\":{\"id\":";
        quote(o,format_osm_id(kind==1?d.streets[i].id:d.administrative_areas[i].id));o<<",\"name\":";quote(o,text(d,kind==1?d.streets[i].name:d.administrative_areas[i].name));o<<"},\"geometry\":";geometry(o,d,kind,i);o<<'}';}
    o<<"]}";return o.str();
}
}
