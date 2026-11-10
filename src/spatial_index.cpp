#include "spatial_index.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <queue>
#include <sstream>
#include <stdexcept>

namespace sheet1 {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr double earth = 6371008.8;
constexpr double rad = pi / 180.0;
using V = std::array<double, 3>;
V sub(V a, V b) { return {a[0]-b[0], a[1]-b[1], a[2]-b[2]}; }
V add(V a, V b) { return {a[0]+b[0], a[1]+b[1], a[2]+b[2]}; }
V mul(V a, double s) { return {a[0]*s,a[1]*s,a[2]*s}; }
double dot(V a, V b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
V cross(V a,V b) { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
double norm(V a) { return std::sqrt(dot(a,a)); }
V unit(Coordinate p) {
    const double lat=p.latitude_e7*1e-7*rad, lon=p.longitude_e7*1e-7*rad;
    return {std::cos(lat)*std::cos(lon),std::cos(lat)*std::sin(lon),std::sin(lat)};
}
Coordinate coordinate(V v) {
    return {static_cast<std::int32_t>(std::llround(std::atan2(v[2],std::hypot(v[0],v[1]))/rad*1e7)),
            static_cast<std::int32_t>(std::llround(std::atan2(v[1],v[0])/rad*1e7))};
}
double angle(V a,V b) { return std::atan2(norm(cross(a,b)),dot(a,b)); }
struct Box {
    V lo{INFINITY,INFINITY,INFINITY}, hi{-INFINITY,-INFINITY,-INFINITY};
    void extend(V p) { for(std::size_t d=0;d<3;++d) { lo[d]=std::min(lo[d],p[d]); hi[d]=std::max(hi[d],p[d]); } }
    void extend(const Box& b) { extend(b.lo); extend(b.hi); }
    bool intersects(const Box& b) const { for(std::size_t d=0;d<3;++d) if(hi[d]<b.lo[d] || lo[d]>b.hi[d]) return false; return true; }
    double distance2(V p) const { double s=0; for(std::size_t d=0;d<3;++d) { double v=std::max({lo[d]-p[d],0.0,p[d]-hi[d]}); s+=v*v; } return s; }
};
struct Entry { Box box; std::size_t id; };
struct Tree {
    struct Node { Box box; std::size_t begin=0,end=0,left=0,right=0; };
    std::vector<Entry> entries;
    std::vector<Node> nodes;
    std::size_t build(std::size_t begin,std::size_t end) {
        const auto n=nodes.size(); nodes.emplace_back();
        Box b; for(auto i=begin;i<end;++i) b.extend(entries[i].box);
        nodes[n].box=b; nodes[n].begin=begin; nodes[n].end=end;
        if(end-begin>16) {
            std::size_t axis=0; for(std::size_t d=1;d<3;++d) if(b.hi[d]-b.lo[d]>b.hi[axis]-b.lo[axis]) axis=d;
            const auto mid=begin+(end-begin)/2;
            std::nth_element(entries.begin()+static_cast<std::ptrdiff_t>(begin),entries.begin()+static_cast<std::ptrdiff_t>(mid),entries.begin()+static_cast<std::ptrdiff_t>(end),[axis](const Entry& a,const Entry& c){ return a.box.lo[axis]+a.box.hi[axis]<c.box.lo[axis]+c.box.hi[axis]; });
            const auto l=build(begin,mid), r=build(mid,end); nodes[n].left=l; nodes[n].right=r;
        }
        return n;
    }
    void finish() { if(!entries.empty()) { nodes.reserve(entries.size()/4+1); build(0,entries.size()); } }
    template<class F> void query_node(std::size_t n,const Box& b,F& f) const {
        const auto& v=nodes[n]; if(!v.box.intersects(b)) return;
        if(v.right) { query_node(v.left,b,f); query_node(v.right,b,f); }
        else for(auto i=v.begin;i<v.end;++i) if(entries[i].box.intersects(b)) f(entries[i].id);
    }
    template<class F> void query(const Box& b,F&& f) const { if(!nodes.empty()) query_node(0,b,f); }
    template<class F> bool query_until_node(std::size_t n,const Box& b,F& f) const {
        const auto& v=nodes[n];if(!v.box.intersects(b))return true;
        if(v.right)return query_until_node(v.left,b,f)&&query_until_node(v.right,b,f);
        for(auto i=v.begin;i<v.end;++i)if(entries[i].box.intersects(b)&&!f(entries[i].id))return false;
        return true;
    }
    template<class F> void query_until(const Box& b,F&& f) const {if(!nodes.empty())query_until_node(0,b,f);}
    // Branch-and-bound. bound converts squared box distance into a safe metric lower bound.
    template<class Bound,class Visit> void nearest(V p,Bound bound,Visit visit,double& best) const {
        if(nodes.empty()) return;
        using Item=std::pair<double,std::size_t>;
        std::priority_queue<Item,std::vector<Item>,std::greater<Item>> queue;
        queue.emplace(bound(nodes[0].box.distance2(p)),0);
        while(!queue.empty()) {
            auto [lower,n]=queue.top(); queue.pop(); if(lower>best) break;
            const auto& v=nodes[n];
            if(v.right) { for(auto child:{v.left,v.right}) { const double d=bound(nodes[child].box.distance2(p)); if(d<=best) queue.emplace(d,child); } }
            else for(auto i=v.begin;i<v.end;++i) if(bound(entries[i].box.distance2(p))<=best) visit(entries[i].id);
        }
    }
};
Box segment_box(V a,V b) { Box box; box.extend(a);box.extend(b); return box; }
double orient(V a,V b,V c) { return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]); }
bool on_segment(V a,V b,V p) {
    constexpr double eps=1e-12;
    const double length=std::hypot(b[0]-a[0],b[1]-a[1]);
    if(length==0)return std::hypot(p[0]-a[0],p[1]-a[1])<=eps;
    // Cross products have squared-coordinate units: scale tolerance by edge length.
    return std::abs(orient(a,b,p))<=eps*length &&
        p[0]>=std::min(a[0],b[0])-eps && p[0]<=std::max(a[0],b[0])+eps &&
        p[1]>=std::min(a[1],b[1])-eps && p[1]<=std::max(a[1],b[1])+eps;
}
bool intersects(V a,V b,V c,V d) {
    if(!segment_box(a,b).intersects(segment_box(c,d))) return false;
    const double x=orient(a,b,c),y=orient(a,b,d),z=orient(c,d,a),w=orient(c,d,b);
    return ((x<0)!=(y<0) && (z<0)!=(w<0)) || on_segment(a,b,c)||on_segment(a,b,d)||on_segment(c,d,a)||on_segment(c,d,b);
}
template<class T> void unique(std::vector<T>& v) { std::sort(v.begin(),v.end());v.erase(std::unique(v.begin(),v.end()),v.end()); }
struct Ring {
    std::vector<V> points;
    Box box;
    std::vector<std::vector<std::uint32_t>> buckets;
    std::size_t bucket(double y) const {
        if(box.hi[1]<=box.lo[1]) return 0;
        return std::min(buckets.size()-1,static_cast<std::size_t>(std::max(0.0,(y-box.lo[1])/(box.hi[1]-box.lo[1])*static_cast<double>(buckets.size()))));
    }
    void prepare() {
        for(auto p:points) box.extend(p);
        buckets.resize(std::clamp<std::size_t>(static_cast<std::size_t>(std::sqrt(points.size())),1,512));
        for(std::size_t i=0;i<points.size();++i) {
            auto a=bucket(std::min(points[i][1],points[(i+1)%points.size()][1]));
            auto b=bucket(std::max(points[i][1],points[(i+1)%points.size()][1]));
            for(auto k=a;k<=b;++k) buckets[k].push_back(static_cast<std::uint32_t>(i));
        }
    }
    // -1 boundary, 0 outside, 1 inside. Half-open edge rule avoids double-counting vertices.
    int contains(V p) const {
        if(!box.intersects(segment_box(p,p))) return 0;
        bool inside=false;
        for(auto i:buckets[bucket(p[1])]) {
            auto a=points[i],b=points[(i+1)%points.size()];
            if(on_segment(a,b,p)) return -1;
            if((a[1]>p[1])!=(b[1]>p[1]) && p[0]<(b[0]-a[0])*(p[1]-a[1])/(b[1]-a[1])+a[0]) inside=!inside;
        }
        return inside?1:0;
    }
};
struct PreparedArea { std::vector<Ring> rings; std::vector<bool> inner; Box box; };
struct Edge { V a,b; std::uint32_t area; };
// Exact closest point on a minor great-circle arc, including its endpoints.
std::pair<double,Coordinate> segment_distance(Coordinate p,Coordinate a,Coordinate b) {
    V q=unit(p),u=unit(a),v=unit(b),n=cross(u,v); double nn=norm(n);
    V closest=angle(q,u)<=angle(q,v)?u:v;
    if(nn>1e-15) {
        n=mul(n,1/nn); auto foot=sub(q,mul(n,dot(q,n))); const double length=norm(foot);
        if(length>1e-15) { foot=mul(foot,1/length); if(dot(cross(u,foot),n)>=-1e-14 && dot(cross(foot,v),n)>=-1e-14 && angle(q,foot)<angle(q,closest)) closest=foot; }
    }
    return {earth*angle(q,closest),coordinate(closest)};
}
} // namespace

struct SpatialIndex::Impl {
    const DataStore& data;
    V center=unit({510000000,100000000});
    V east{-std::sin(10*rad),std::cos(10*rad),0};
    V north=cross(center,east);
    double max_rho2=0;
    Tree buildings, regions, streets, boundaries;
    std::vector<PreparedArea> areas;
    std::vector<Edge> edges;
    V project(Coordinate p) const {
        auto u=unit(p); const double c=dot(u,center);
        if(c<=1e-6) throw std::runtime_error("Geometry outside supported Germany-centred hemisphere");
        return {dot(u,east)/c,dot(u,north)/c,0};
    }
    explicit Impl(const DataStore& d):data(d),areas(d.administrative_areas.size()) {
        buildings.entries.reserve(d.buildings.size());
        for(std::size_t i=0;i<d.buildings.size();++i) if(d.buildings[i].has_point) {auto u=unit(d.buildings[i].point); buildings.entries.push_back({segment_box(u,u),i});}
        buildings.finish();
        for(std::size_t i=0;i<areas.size();++i) {
            const auto& src=d.administrative_areas[i]; if(!src.has_geometry) continue;
            auto& area=areas[i];
            for(std::size_t j=src.rings_begin;j<src.rings_begin+src.rings_count;++j) {
                const auto& raw=d.administrative_rings[j]; Ring ring;
                for(std::size_t k=raw.points_begin;k<raw.points_begin+raw.points_count;++k) ring.points.push_back(project(d.administrative_points[k]));
                if(ring.points.size()<3) continue;
                ring.prepare(); area.box.extend(ring.box);
                for(std::size_t k=0;k<ring.points.size();++k) {
                    auto a=ring.points[k],b=ring.points[(k+1)%ring.points.size()];
                    const auto id=edges.size();edges.push_back({a,b,static_cast<std::uint32_t>(i)});
                    boundaries.entries.push_back({segment_box(a,b),id});
                    max_rho2=std::max(max_rho2,dot(a,a));
                }
                area.rings.push_back(std::move(ring));area.inner.push_back(raw.is_inner);
            }
            if(!area.rings.empty()) regions.entries.push_back({area.box,i});
        }
        regions.finish(); boundaries.finish();
        for(std::size_t i=0;i<d.streets.size();++i) {
            const auto& s=d.streets[i];
            Box box;
            for(std::size_t j=0;j<s.points_count;++j) {auto u=project(d.street_points[s.points_begin+j]);box.extend(u);max_rho2=std::max(max_rho2,dot(u,u));}
            if(s.points_count) streets.entries.push_back({box,i});
        }
        streets.finish();
    }
    bool contains(std::size_t i,V p) const {
        const auto& a=areas[i]; bool inside=false,component=false;
        for(std::size_t j=0;j<a.rings.size();++j) {
            const int hit=a.rings[j].contains(p); if(hit==-1) return true;
            if(!a.inner[j]) { inside=inside||component;component=hit==1; }
            else if(hit==1) component=false;
        }
        return inside||component;
    }
    void at(V p,std::vector<std::uint32_t>& out) const {
        regions.query(segment_box(p,p),[&](std::size_t i){ if(contains(i,p)) out.push_back(static_cast<std::uint32_t>(i)); });
    }
    void path(const std::vector<Coordinate>& points,std::size_t begin,std::size_t count,std::vector<std::uint32_t>& out) const {
        if(!count) return;
        V a=project(points[begin]); at(a,out);
        for(std::size_t j=1;j<count;++j) {
            V b=project(points[begin+j]);
            boundaries.query(segment_box(a,b),[&](std::size_t e){const auto& edge=edges[e];if(intersects(a,b,edge.a,edge.b)) out.push_back(edge.area);});
            a=b;
        }
        // Every region entered/exited crosses a boundary; endpoints handle fully contained paths.
        if(count>1) at(a,out);
    }
};

SpatialIndex::SpatialIndex(DataStore& data):impl_(std::make_unique<Impl>(data)) {}
SpatialIndex::~SpatialIndex()=default;
bool SpatialIndex::covers(std::size_t area,Coordinate p) const { return area<impl_->areas.size() && impl_->contains(area,impl_->project(p)); }
std::vector<std::uint32_t> SpatialIndex::regions_at(Coordinate p) const {
    std::vector<std::uint32_t> out; impl_->at(impl_->project(p),out); unique(out);return out;
}
std::vector<NearestObject> SpatialIndex::nearest_buildings(Coordinate p,std::size_t count) const {
    std::vector<NearestObject> out;if(!count) return out;
    V q=unit(p);double best=INFINITY;
    auto less=[&](const NearestObject& a,const NearestObject& b){
        if(a.distance_metres!=b.distance_metres) return a.distance_metres<b.distance_metres;
        const auto x=impl_->data.buildings[a.index].id,y=impl_->data.buildings[b.index].id;
        return x.type!=y.type?x.type<y.type:x.value<y.value;
    };
    impl_->buildings.nearest(q,[](double s){return earth*2*std::asin(std::min(1.0,std::sqrt(s)/2));},[&](std::size_t i){
        const auto& b=impl_->data.buildings[i];if(b.building_type==kInvalidStringId || impl_->data.strings.resolve(b.building_type)=="no") return;
        out.push_back({NearestObject::Kind::building,i,earth*angle(q,unit(b.point)),b.point});std::sort(out.begin(),out.end(),less);
        if(out.size()>count) out.pop_back(); if(out.size()==count) best=out.back().distance_metres+1e-8;
    },best);return out;
}
std::vector<NearestObject> SpatialIndex::reverse(Coordinate p) const {
    auto out=nearest_buildings(p,1);const auto q=impl_->project(p);
    double best=INFINITY; NearestObject street{NearestObject::Kind::street,0,INFINITY,p};
    // On the gnomonic plane, inverse projection contracts by at most 1/(1+rho^2).
    const double factor=earth/(1+std::max(impl_->max_rho2,dot(q,q)));
    impl_->streets.nearest(q,[factor](double s){return factor*std::sqrt(s);},[&](std::size_t i){
        const auto& s=impl_->data.streets[i];
        for(std::size_t j=0;j<s.points_count;++j) {
            auto a=impl_->data.street_points[s.points_begin+j],b=impl_->data.street_points[s.points_begin+(j?j-1:0)];
            auto [dist,point]=segment_distance(p,a,b);
            if(dist<best) {best=dist; street={NearestObject::Kind::street,i,dist,point};}
        }
    },best);
    if(std::isfinite(best)) out.push_back(street);
    std::sort(out.begin(),out.end(),[](auto& a,auto& b){return a.distance_metres<b.distance_metres;});
    // Regions are context/fallback, rather than zero-distance competitors to every address.
    if(out.empty() || out.front().distance_metres>1000) {
        auto context=regions_at(p);
        std::sort(context.begin(),context.end(),[&](auto a,auto b){return impl_->data.administrative_areas[a].admin_level>impl_->data.administrative_areas[b].admin_level;});
        if(!context.empty()) out.insert(out.begin(),{NearestObject::Kind::area,context.front(),0,p});
        else {
            double area_best=out.empty()?INFINITY:out.front().distance_metres;
            NearestObject area{NearestObject::Kind::area,0,INFINITY,p};
            auto unproject=[&](V v){return coordinate(add(impl_->center,add(mul(impl_->east,v[0]),mul(impl_->north,v[1]))));};
            impl_->boundaries.nearest(q,[factor](double s){return factor*std::sqrt(s);},[&](std::size_t i){
                const auto& e=impl_->edges[i];auto [distance,point]=segment_distance(p,unproject(e.a),unproject(e.b));
                if(distance<area_best){area_best=distance;area={NearestObject::Kind::area,e.area,distance,point};}
            },area_best);
            if(std::isfinite(area.distance_metres))out.insert(out.begin(),area);
        }
    }
    return out;
}
void SpatialIndex::assign_regions(DataStore& d) const {
    auto start=std::chrono::steady_clock::now();d.region_memberships.clear();
    std::vector<std::uint32_t> ids;ids.reserve(32);
    auto save=[&](auto& obj){unique(ids);obj.regions_begin=d.region_memberships.size();obj.regions_count=static_cast<std::uint32_t>(ids.size());d.region_memberships.insert(d.region_memberships.end(),ids.begin(),ids.end());ids.clear();};
    for(std::size_t i=0;i<d.buildings.size();++i) {
        auto& b=d.buildings[i];if(b.has_point) impl_->at(impl_->project(b.point),ids);save(b);
        if((i+1)%1000000==0) std::cout<<"Assigned regions: "<<i+1<<" buildings\n"<<std::flush;
    }
    std::cout<<"Assigning crossed regions to streets...\n"<<std::flush;
    std::size_t completed=0;
    for(auto& s:d.streets) {impl_->path(d.street_points,s.points_begin,s.points_count,ids);save(s);if(++completed%500000==0)std::cout<<"Assigned regions: "<<completed<<" streets\n"<<std::flush;}
    std::cout<<"Assigning overlapping regions to areas...\n"<<std::flush;
    for(std::size_t i=0;i<d.administrative_areas.size();++i) {
        auto& a=d.administrative_areas[i];
        for(std::size_t j=a.rings_begin;j<a.rings_begin+a.rings_count;++j) {
            const auto& r=d.administrative_rings[j];impl_->path(d.administrative_points,r.points_begin,r.points_count,ids);
        }
        // Also include regions entirely enclosed by this area, without a boundary crossing.
        if(a.has_geometry) impl_->regions.query(impl_->areas[i].box,[&](std::size_t j){
            if(j==i)return;
            for(std::size_t k=0;k<impl_->areas[j].rings.size();++k)
                if(!impl_->areas[j].inner[k] && impl_->contains(i,impl_->areas[j].rings[k].points[0])) {ids.push_back(static_cast<std::uint32_t>(j));break;}
        });
        ids.erase(std::remove(ids.begin(),ids.end(),i),ids.end());save(a);
    }
    std::cout<<"Region memberships: "<<d.region_memberships.size()<<"; assignment seconds: "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<'\n'<<std::flush;
}
std::vector<std::size_t> SpatialIndex::viewport(Coordinate minimum,Coordinate maximum,int kind,std::size_t limit,std::size_t stride) const {
    std::vector<std::size_t> out;
    if(limit==0)return out;
    if(stride==0)throw std::invalid_argument("Stride must be positive");
    auto inside=[&](Coordinate p){return p.latitude_e7>=minimum.latitude_e7 && p.latitude_e7<=maximum.latitude_e7 && p.longitude_e7>=minimum.longitude_e7 && p.longitude_e7<=maximum.longitude_e7;};
    if(kind==0) {
        Coordinate c{static_cast<std::int32_t>((static_cast<std::int64_t>(minimum.latitude_e7)+maximum.latitude_e7)/2),static_cast<std::int32_t>((static_cast<std::int64_t>(minimum.longitude_e7)+maximum.longitude_e7)/2)};
        auto u=unit(c); const double radius=std::min(2.0,((maximum.latitude_e7-static_cast<double>(minimum.latitude_e7))+(maximum.longitude_e7-static_cast<double>(minimum.longitude_e7)))*1e-7*rad/2+1e-8);
        Box box;box.extend(sub(u,{radius,radius,radius}));box.extend(add(u,{radius,radius,radius}));
        std::size_t matched=0;
        impl_->buildings.query_until(box,[&](std::size_t i){if(inside(impl_->data.buildings[i].point) && matched++%stride==0) out.push_back(i);return out.size()<limit;});
    } else {
        // Bound the projection of a spherical cap enclosing the whole viewport.
        // The maximum gnomonic differential over that cap is sec^2(extent).
        Coordinate center{static_cast<std::int32_t>((static_cast<std::int64_t>(minimum.latitude_e7)+maximum.latitude_e7)/2),static_cast<std::int32_t>((static_cast<std::int64_t>(minimum.longitude_e7)+maximum.longitude_e7)/2)};
        const double radius=((maximum.latitude_e7-static_cast<double>(minimum.latitude_e7))+(maximum.longitude_e7-static_cast<double>(minimum.longitude_e7)))*1e-7*rad/2+1e-8;
        const double extent=angle(unit(center),impl_->center)+radius;
        Box box;
        if(extent>=pi/2-1e-6) {box.lo={-1e20,-1e20,0};box.hi={1e20,1e20,0};}
        else {auto p=impl_->project(center);const double pad=radius/std::pow(std::cos(extent),2);box.extend(sub(p,{pad,pad,0}));box.extend(add(p,{pad,pad,0}));}
        if(kind==1) impl_->streets.query_until(box,[&](std::size_t i){out.push_back(i);return out.size()<limit;});
        else impl_->regions.query(box,[&](std::size_t i){out.push_back(i);});
        unique(out);if(out.size()>limit) out.resize(limit);
    }
    return out;
}
void run_reverse_file(const SpatialIndex& index,const DataStore& data,const std::filesystem::path& input_path,const std::filesystem::path& output_path) {
    std::ifstream input(input_path);if(!input) throw std::runtime_error("Cannot open reverse input");
    // Validate the whole input before truncating the destination.
    std::vector<Coordinate> queries;std::string line;std::size_t number=0;
    while(std::getline(input,line)) {++number;double lat,lon;std::string extra;std::istringstream row(line);
        if(!(row>>lat>>lon) || row>>extra || !std::isfinite(lat)||!std::isfinite(lon)||std::abs(lat)>90||std::abs(lon)>180) throw std::runtime_error("Invalid latitude longitude on line "+std::to_string(number));
        queries.push_back({static_cast<std::int32_t>(std::llround(lat*1e7)),static_cast<std::int32_t>(std::llround(lon*1e7))});
    }
    if(input.bad())throw std::runtime_error("Cannot read reverse input");
    if(std::filesystem::absolute(input_path).lexically_normal()==std::filesystem::absolute(output_path).lexically_normal()) throw std::runtime_error("Input and output paths must differ");
    std::error_code ec;
    if(std::filesystem::equivalent(input_path,output_path,ec)) throw std::runtime_error("Input and output refer to the same file");
    std::ofstream output(output_path);if(!output) throw std::runtime_error("Cannot open reverse output");
    const auto start=std::chrono::steady_clock::now();
    for(auto q:queries) {auto hits=index.nearest_buildings(q);for(std::size_t i=0;i<hits.size();++i) {if(i)output<<", ";output<<format_osm_id(data.buildings[hits[i].index].id);}output<<'\n';}
    output.flush();
    if(!output) throw std::runtime_error("Cannot write reverse output");
    std::cout<<"Reverse queries: "<<queries.size()<<"; query seconds: "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<'\n';
}
} // namespace sheet1
