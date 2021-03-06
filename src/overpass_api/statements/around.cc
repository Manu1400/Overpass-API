/** Copyright 2008, 2009, 2010, 2011, 2012 Roland Olbricht
*
* This file is part of Overpass_API.
*
* Overpass_API is free software: you can redistribute it and/or modify
* it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* Overpass_API is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with Overpass_API.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "../../template_db/block_backend.h"
#include "../../template_db/random_file.h"
#include "../core/settings.h"
#include "../data/collect_members.h"
#include "around.h"
#include "recurse.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>

using namespace std;

//-----------------------------------------------------------------------------

Uint32_Index inc(Uint32_Index idx)
{
  return Uint32_Index(idx.val() + 1);
}

Uint32_Index dec(Uint32_Index idx)
{
  return Uint32_Index(idx.val() - 1);
}

Uint31_Index inc(Uint31_Index idx)
{
  if (idx.val() & 0x80000000)
    return Uint31_Index((idx.val() & 0x7fffffff) + 1);
  else
    return Uint31_Index(idx.val() | 0x80000000);
}


template < class TIndex, class TObject >
set< pair< TIndex, TIndex > > ranges(const map< TIndex, vector< TObject > >& elems)
{
  set< pair< TIndex, TIndex > > result;
  if (elems.empty())
    return result;
  pair< TIndex, TIndex > range = make_pair(elems.begin()->first, inc(elems.begin()->first));
  for (typename map< TIndex, vector< TObject > >::const_iterator
      it = elems.begin(); it != elems.end(); ++it)
  {
    if (!(range.second < it->first))
      range.second = inc(it->first);
    else
    {
      result.insert(range);
      range = make_pair(it->first, inc(it->first));
    }
  }
  result.insert(range);
  
  return result;
}


set< pair< Uint32_Index, Uint32_Index > > ranges(double lat, double lon)
{
  Uint32_Index idx = ::ll_upper_(lat, lon);
  set< pair< Uint32_Index, Uint32_Index > > result;
  result.insert(make_pair(idx, inc(idx)));
  return result;
}


template < class TIndex >
set< pair< TIndex, TIndex > > set_union_(const set< pair< TIndex, TIndex > >& a,
					 const set< pair< TIndex, TIndex > >& b)
{
  set< pair< TIndex, TIndex > > temp;
  set_union(a.begin(), a.end(), b.begin(), b.end(),
	    insert_iterator< set< pair< TIndex, TIndex > > >(temp, temp.begin()));
  
  set< pair< TIndex, TIndex > > result;
  if (temp.empty())
    return result;
  
  typename set< pair< TIndex, TIndex > >::const_iterator it = temp.begin();
  TIndex last_first = it->first;
  TIndex last_second = it->second;
  ++it;
  for (; it != temp.end(); ++it)
  {
    if (last_second < it->first)
    {
      result.insert(make_pair(last_first, last_second));
      last_first = it->first;
    }
    if (last_second < it->second)
      last_second = it->second;
  }
  result.insert(make_pair(last_first, last_second));

  return result;
}

set< pair< Uint32_Index, Uint32_Index > > blockwise_split
    (const set< pair< Uint32_Index, Uint32_Index > >& idxs)
{
  set< pair< Uint32_Index, Uint32_Index > > result;
  
  for (set< pair< Uint32_Index, Uint32_Index > >::const_iterator it = idxs.begin();
      it != idxs.end(); ++it)
  {
    uint32 start = it->first.val();
    while (start < it->second.val())
    {
      uint32 end;
      if ((start & 0x3) != 0 || it->second.val() < start + 0x4)
	end = start + 1;
      else if ((start & 0x3c) != 0 || it->second.val() < start + 0x40)
	end = start + 0x4;
      else if ((start & 0x3c0) != 0 || it->second.val() < start + 0x400)
	end = start + 0x40;
      else if ((start & 0x3c00) != 0 || it->second.val() < start + 0x4000)
	end = start + 0x400;
      else if ((start & 0x3c000) != 0 || it->second.val() < start + 0x40000)
	end = start + 0x4000;
      else if ((start & 0x3c0000) != 0 || it->second.val() < start + 0x400000)
	end = start + 0x40000;
      else if ((start & 0x3c00000) != 0 || it->second.val() < start + 0x4000000)
	end = start + 0x400000;
      else
	end = start + 0x4000000;
      
      result.insert(make_pair(Uint32_Index(start), Uint32_Index(end)));
      start = end;
    }
  }
  
  return result;
}

set< pair< Uint32_Index, Uint32_Index > > calc_ranges_
    (double south, double north, double west, double east)
{
  vector< pair< uint32, uint32 > > ranges = calc_ranges(south, north, west, east);
  
  set< pair< Uint32_Index, Uint32_Index > > result;
  for (vector< pair< uint32, uint32 > >::const_iterator it = ranges.begin();
      it != ranges.end(); ++it)
    result.insert(make_pair(Uint32_Index(it->first), Uint32_Index(it->second)));
  
  return result;
}

set< pair< Uint32_Index, Uint32_Index > > expand
    (const set< pair< Uint32_Index, Uint32_Index > >& idxs, double radius)
{
  set< pair< Uint32_Index, Uint32_Index > > blockwise_idxs = blockwise_split(idxs);

  set< pair< Uint32_Index, Uint32_Index > > result;
  for (set< pair< Uint32_Index, Uint32_Index > >::const_iterator it = blockwise_idxs.begin();
      it != blockwise_idxs.end(); ++it)
  {
    double south = ::lat(it->first.val(), 0) - radius*(90.0/10/1000/1000);
    double north = ::lat(dec(it->second).val(), 0xffffffff) + radius*(90.0/10/1000/1000);
    double lon_factor = cos((-north < south ? north : south)*(acos(0)/90.0));
    double west = ::lon(it->first.val(), 0) - radius*(90.0/10/1000/1000)/lon_factor;
    double east = ::lon(dec(it->second).val(), 0xffffffff)
        + radius*(90.0/10/1000/1000)/lon_factor;
    
    result = set_union_(result, calc_ranges_(south, north, west, east));
  }
  
  return result;
}

set< pair< Uint32_Index, Uint32_Index > > children
    (const set< pair< Uint31_Index, Uint31_Index > >& way_rel_idxs)
{
  set< pair< Uint32_Index, Uint32_Index > > result;

  vector< pair< uint32, uint32 > > ranges;
  
  for (set< pair< Uint31_Index, Uint31_Index > >::const_iterator it = way_rel_idxs.begin();
      it != way_rel_idxs.end(); ++it)
  {
    for (Uint31_Index idx = it->first; idx < it->second; idx = inc(idx))
    {
      if (idx.val() & 0x80000000)
      {
	uint32 lat = 0;
	uint32 lon = 0;      
	uint32 offset = 0;
	
	if (idx.val() & 0x00000001)
	{
	  lat = upper_ilat(idx.val() & 0x2aaaaaa8);
	  lon = upper_ilon(idx.val() & 0x55555554);
	  offset = 2;
	}
	else if (idx.val() & 0x00000002)
	{
	  lat = upper_ilat(idx.val() & 0x2aaaaa80);
	  lon = upper_ilon(idx.val() & 0x55555540);
	  offset = 8;
	}
	else if (idx.val() & 0x00000004)
	{
	  lat = upper_ilat(idx.val() & 0x2aaaa800);
	  lon = upper_ilon(idx.val() & 0x55555400);
	  offset = 0x20;
	}
	else if (idx.val() & 0x00000008)
	{
	  lat = upper_ilat(idx.val() & 0x2aaa8000);
	  lon = upper_ilon(idx.val() & 0x55554000);
	  offset = 0x80;
	}
	else if (idx.val() & 0x00000010)
	{
	  lat = upper_ilat(idx.val() & 0x2aa80000);
	  lon = upper_ilon(idx.val() & 0x55540000);
	  offset = 0x200;
	}
	else if (idx.val() & 0x00000020)
	{
	  lat = upper_ilat(idx.val() & 0x2a800000);
	  lon = upper_ilon(idx.val() & 0x55400000);
	  offset = 0x800;
	}
	else if (idx.val() & 0x00000040)
	{
	  lat = upper_ilat(idx.val() & 0x28000000);
	  lon = upper_ilon(idx.val() & 0x54000000);
	  offset = 0x2000;
	}
	else // idx.val() == 0x80000080
	{
	  lat = 0;
	  lon = 0;
	  offset = 0x8000;
	}
	
	ranges.push_back(make_pair(ll_upper(lat<<16, lon<<16),
				   ll_upper((lat+offset-1)<<16, (lon+offset-1)<<16)+1));
	ranges.push_back(make_pair(ll_upper(lat<<16, (lon+offset)<<16),
				   ll_upper((lat+offset-1)<<16, (lon+2*offset-1)<<16)+1));
        ranges.push_back(make_pair(ll_upper((lat+offset)<<16, lon<<16),
				   ll_upper((lat+2*offset-1)<<16, (lon+offset-1)<<16)+1));
	ranges.push_back(make_pair(ll_upper((lat+offset)<<16, (lon+offset)<<16),
				   ll_upper((lat+2*offset-1)<<16, (lon+2*offset-1)<<16)+1));
      }
      else
	ranges.push_back(make_pair(idx.val(), idx.val() + 1));
    }
  }

  sort(ranges.begin(), ranges.end());
  uint32 pos = 0;
  for (vector< pair< uint32, uint32 > >::const_iterator it = ranges.begin();
      it != ranges.end(); ++it)
  {
    if (pos < it->first)
      pos = it->first;
    result.insert(make_pair(Uint32_Index(pos), Uint32_Index(it->second)));
  }
  
  return result;
}

//-----------------------------------------------------------------------------

class Around_Constraint : public Query_Constraint
{
  public:
    Around_Constraint(Around_Statement& around_) : around(&around_), ranges_used(false) {}
    
    bool delivers_data() { return true; }
    
    bool get_ranges
        (Resource_Manager& rman, set< pair< Uint32_Index, Uint32_Index > >& ranges);
    bool get_ranges
        (Resource_Manager& rman, set< pair< Uint31_Index, Uint31_Index > >& ranges);
    void filter(Resource_Manager& rman, Set& into);
    void filter(const Statement& query, Resource_Manager& rman, Set& into);
    virtual ~Around_Constraint() {}
    
  private:
    Around_Statement* around;
    bool ranges_used;
};

bool Around_Constraint::get_ranges
    (Resource_Manager& rman, set< pair< Uint32_Index, Uint32_Index > >& ranges)
{
  ranges_used = true;
  
  ranges = around->calc_ranges(rman.sets()[around->get_source_name()], rman);
  return true;
}

bool Around_Constraint::get_ranges
    (Resource_Manager& rman, set< pair< Uint31_Index, Uint31_Index > >& ranges)
{
  set< pair< Uint32_Index, Uint32_Index > > node_ranges;
  this->get_ranges(rman, node_ranges);
  ranges = calc_parents(node_ranges);
  return true;
}

void Around_Constraint::filter(Resource_Manager& rman, Set& into)
{
  // pre-process ways to reduce the load of the expensive filter
  if (ranges_used == false)
  {
    // pre-filter nodes
    {
      set< pair< Uint32_Index, Uint32_Index > > ranges;
      get_ranges(rman, ranges);
      
      set< pair< Uint32_Index, Uint32_Index > >::const_iterator ranges_it = ranges.begin();
      map< Uint32_Index, vector< Node_Skeleton > >::iterator it = into.nodes.begin();
      for (; it != into.nodes.end() && ranges_it != ranges.end(); )
      {
        if (!(it->first < ranges_it->second))
	  ++ranges_it;
        else if (!(it->first < ranges_it->first))
	  ++it;
        else
        {
	  it->second.clear();
	  ++it;
        }
      }
      for (; it != into.nodes.end(); ++it)
        it->second.clear();
    }
    
    set< pair< Uint31_Index, Uint31_Index > > ranges;
    get_ranges(rman, ranges);
    
    // pre-filter ways
    filter_ways_by_ranges(into.ways, ranges);

    // pre-filter relations
    {
      set< pair< Uint31_Index, Uint31_Index > >::const_iterator ranges_it = ranges.begin();
      map< Uint31_Index, vector< Relation_Skeleton > >::iterator it = into.relations.begin();
      for (; it != into.relations.end() && ranges_it != ranges.end(); )
      {
        if (!(it->first < ranges_it->second))
	  ++ranges_it;
        else if (!(it->first < ranges_it->first))
	  ++it;
        else
        {
	  it->second.clear();
	  ++it;
        }
      }
      for (; it != into.relations.end(); ++it)
        it->second.clear();
    }
  }
  ranges_used = false;
  
  //TODO: areas
}


struct Relation_Member_Collection
{
  Relation_Member_Collection(const map< Uint31_Index, vector< Relation_Skeleton > >& relations,
			     const Statement& query, Resource_Manager& rman,
			     set< pair< Uint32_Index, Uint32_Index > >* node_ranges,
			     set< pair< Uint31_Index, Uint31_Index > >* way_ranges)
      : query_(query),
	way_members(relation_way_members(&query, rman, relations, way_ranges)),
        node_members(relation_node_members(&query, rman, relations, node_ranges))
  {
    // Retrieve all nodes referred by the ways.    
/*    if (node_ranges)
      collect_nodes(query, rman, relations.begin(), relations.end(), node_members, *node_ranges);
    else
      collect_nodes(query, rman, relations.begin(), relations.end(), node_members);*/
    
    // Order node ids by id.
    for (map< Uint32_Index, vector< Node_Skeleton > >::iterator it = node_members.begin();
        it != node_members.end(); ++it)
    {
      for (vector< Node_Skeleton >::const_iterator iit = it->second.begin();
          iit != it->second.end(); ++iit)
        node_members_by_id.push_back(make_pair(it->first, &*iit));
    }
    Order_By_Node_Id order_by_node_id;
    sort(node_members_by_id.begin(), node_members_by_id.end(), order_by_node_id);
    
    // Retrieve all ways referred by the relations.
/*    if (way_ranges)
      collect_ways(query, rman, relations.begin(), relations.end(), way_members, *way_ranges);
    else
      collect_ways(query, rman, relations.begin(), relations.end(), way_members);*/
    
    // Order way ids by id.
    for (map< Uint31_Index, vector< Way_Skeleton > >::iterator it = way_members.begin();
        it != way_members.end(); ++it)
    {
      for (vector< Way_Skeleton >::const_iterator iit = it->second.begin();
          iit != it->second.end(); ++iit)
        way_members_by_id.push_back(make_pair(it->first, &*iit));
    }
    Order_By_Way_Id order_by_way_id;
    sort(way_members_by_id.begin(), way_members_by_id.end(), order_by_way_id);
  }
  
  const pair< Uint32_Index, const Node_Skeleton* >* get_node_by_id(Uint64 id) const
  {
    const pair< Uint32_Index, const Node_Skeleton* >* node =
        binary_search_for_pair_id(node_members_by_id, id);
    
    return node;
  }

  const pair< Uint31_Index, const Way_Skeleton* >* get_way_by_id(Uint32_Index id) const
  {
    const pair< Uint31_Index, const Way_Skeleton* >* way =
        binary_search_for_pair_id(way_members_by_id, id);
    
    return way;
  }

  const Statement& query_;
  map< Uint31_Index, vector< Way_Skeleton > > way_members;
  map< Uint32_Index, vector< Node_Skeleton > > node_members;
  vector< pair< Uint31_Index, const Way_Skeleton* > > way_members_by_id;
  vector< pair< Uint32_Index, const Node_Skeleton* > > node_members_by_id;
};


void Around_Constraint::filter(const Statement& query, Resource_Manager& rman, Set& into)
{
  around->calc_lat_lons(rman.sets()[around->get_source_name()], *around, rman);
  
  // process nodes
  for (map< Uint32_Index, vector< Node_Skeleton > >::iterator it = into.nodes.begin();
      it != into.nodes.end(); ++it)
  {
    vector< Node_Skeleton > local_into;
    for (vector< Node_Skeleton >::const_iterator iit = it->second.begin();
        iit != it->second.end(); ++iit)
    {
      double lat(::lat(it->first.val(), iit->ll_lower));
      double lon(::lon(it->first.val(), iit->ll_lower));
      if (around->is_inside(lat, lon))
	local_into.push_back(*iit);
    }
    it->second.swap(local_into);
  }
  
  {
    //Process ways

    // Retrieve all nodes referred by the ways.
    Way_Geometry_Store way_geometries(into.ways, query, rman);
    
    for (map< Uint31_Index, vector< Way_Skeleton > >::iterator it = into.ways.begin();
        it != into.ways.end(); ++it)
    {
      vector< Way_Skeleton > local_into;
      for (vector< Way_Skeleton >::const_iterator iit = it->second.begin();
          iit != it->second.end(); ++iit)
      {
	if (around->is_inside(way_geometries.get_geometry(*iit)))
	  local_into.push_back(*iit);
      }
      it->second.swap(local_into);
    }
  }
  {
    //Process relations
    
    // Retrieve all node and way members referred by the relations.
    set< pair< Uint32_Index, Uint32_Index > > node_ranges;
    get_ranges(rman, node_ranges);
    set< pair< Uint31_Index, Uint31_Index > > way_ranges;
    get_ranges(rman, way_ranges);
    Relation_Member_Collection relation_members
        (into.relations, query, rman, &node_ranges, &way_ranges);
        
    // Retrieve all nodes referred by the ways.
    Way_Geometry_Store way_geometries(relation_members.way_members, query, rman);
    
    for (map< Uint31_Index, vector< Relation_Skeleton > >::iterator it = into.relations.begin();
        it != into.relations.end(); ++it)
    {
      vector< Relation_Skeleton > local_into;
      for (vector< Relation_Skeleton >::const_iterator iit = it->second.begin();
          iit != it->second.end(); ++iit)
      {
	for (vector< Relation_Entry >::const_iterator nit = iit->members.begin();
	    nit != iit->members.end(); ++nit)
        {
	  if (nit->type == Relation_Entry::NODE)
	  {
	    const pair< Uint32_Index, const Node_Skeleton* >* second_nd =
	        relation_members.get_node_by_id(nit->ref);
	    if (!second_nd)
	      continue;
	    double lat(::lat(second_nd->first.val(), second_nd->second->ll_lower));
	    double lon(::lon(second_nd->first.val(), second_nd->second->ll_lower));
	  
	    if (around->is_inside(lat, lon))
	    {
	      local_into.push_back(*iit);
	      break;
	    }
	  }
	  else if (nit->type == Relation_Entry::WAY)
	  {
	    const pair< Uint31_Index, const Way_Skeleton* >* second_nd =
	        relation_members.get_way_by_id(nit->ref32());
	    if (!second_nd)
	      continue;
	    if (around->is_inside(way_geometries.get_geometry(*second_nd->second)))
	    {
	      local_into.push_back(*iit);
	      break;
	    }
	  }
        }
      }
      it->second.swap(local_into);
    }
  }  
  
  //TODO: areas
}

//-----------------------------------------------------------------------------

Generic_Statement_Maker< Around_Statement > Around_Statement::statement_maker("around");

Around_Statement::Around_Statement
    (int line_number_, const map< string, string >& input_attributes)
    : Statement(line_number_), lat(100.0), lon(200.0)
{
  map< string, string > attributes;
  
  attributes["from"] = "_";
  attributes["into"] = "_";
  attributes["radius"] = "";
  attributes["lat"] = "";
  attributes["lon"] = "";
  
  eval_attributes_array(get_name(), attributes, input_attributes);
  
  input = attributes["from"];
  output = attributes["into"];
  
  radius = atof(attributes["radius"].c_str());
  if ((radius < 0.0) || (attributes["radius"] == ""))
  {
    ostringstream temp;
    temp<<"For the attribute \"radius\" of the element \"around\""
        <<" the only allowed values are nonnegative floats.";
    add_static_error(temp.str());
  }
  
  if (attributes["lat"] != "")
  {
    lat = atof(attributes["lat"].c_str());
    if ((lat < -90.0) || (lat > 90.0))
    {
      ostringstream temp;
      temp<<"For the attribute \"lat\" of the element \"around\""
          <<" the only allowed values are floats between -90.0 and 90.0 or an empty value.";
      add_static_error(temp.str());
    }
  }
  
  if (attributes["lon"] != "")
  {
    lon = atof(attributes["lon"].c_str());
    if ((lon < -180.0) || (lon > 180.0))
    {
      ostringstream temp;
      temp<<"For the attribute \"lon\" of the element \"around\""
          <<" the only allowed values are floats between -180.0 and 180.0 or an empty value.";
      add_static_error(temp.str());
    }
  }
}

Around_Statement::~Around_Statement()
{
  for (vector< Query_Constraint* >::const_iterator it = constraints.begin();
      it != constraints.end(); ++it)
    delete *it;
}

double great_circle_dist(double lat1, double lon1, double lat2, double lon2)
{
  if (lat1 == lat2 && lon1 == lon2)
    return 0;
  double scalar_prod =
      sin(lat1/90.0*acos(0))*sin(lat2/90.0*acos(0)) +
      cos(lat1/90.0*acos(0))*sin(lon1/90.0*acos(0))*cos(lat2/90.0*acos(0))*sin(lon2/90.0*acos(0)) +
      cos(lat1/90.0*acos(0))*cos(lon1/90.0*acos(0))*cos(lat2/90.0*acos(0))*cos(lon2/90.0*acos(0));
  if (scalar_prod > 1)
    scalar_prod = 1;
  return acos(scalar_prod)*(10*1000*1000/acos(0));
}


vector< double > cartesian(double lat, double lon)
{
  vector< double > result(3);
  
  result[0] = sin(lat/90.0*acos(0));
  result[1] = cos(lat/90.0*acos(0))*sin(lon/90.0*acos(0));
  result[2] = cos(lat/90.0*acos(0))*cos(lon/90.0*acos(0));
  
  return result;
}


void rescale(double a, vector< double >& v)
{
  v[0] *= a;
  v[1] *= a;
  v[2] *= a;
}


vector< double > sum(const vector< double >& v, const vector< double >& w)
{
  vector< double > result(3);
  
  result[0] = v[0] + w[0];
  result[1] = v[1] + w[1];
  result[2] = v[2] + w[2];
  
  return result;
}


double scalar_prod(const vector< double >& v, const vector< double >& w)
{
  return v[0]*w[0] + v[1]*w[1] + v[2]*w[2];
}


vector< double > cross_prod(const vector< double >& v, const vector< double >& w)
{
  vector< double > result(3);
  
  result[0] = v[1]*w[2] - v[2]*w[1];
  result[1] = v[2]*w[0] - v[0]*w[2];
  result[2] = v[0]*w[1] - v[1]*w[0];
  
  return result;
}


  
Prepared_Segment::Prepared_Segment
  (double first_lat_, double first_lon_, double second_lat_, double second_lon_)
  : first_lat(first_lat_), first_lon(first_lon_), second_lat(second_lat_), second_lon(second_lon_)
{
  first_cartesian = cartesian(first_lat, first_lon);
  second_cartesian = cartesian(second_lat, second_lon);
  norm = cross_prod(first_cartesian, second_cartesian);
}


Prepared_Point::Prepared_Point
  (double lat_, double lon_)
  : lat(lat_), lon(lon_)
{
  cartesian = ::cartesian(lat, lon);
}


double great_circle_line_dist(const Prepared_Segment& segment, const vector< double >& cartesian)
{
  double scalar_prod_ = abs(scalar_prod(cartesian, segment.norm))
      /sqrt(scalar_prod(segment.norm, segment.norm));
  
  if (scalar_prod_ > 1)
    scalar_prod_ = 1;
  
  return asin(scalar_prod_)*(10*1000*1000/acos(0));
}


double great_circle_line_dist(double llat1, double llon1, double llat2, double llon2,
                              double plat, double plon)
{
  vector< double > norm = cross_prod(cartesian(llat1, llon1), cartesian(llat2, llon2));
  
  double scalar_prod_ = abs(scalar_prod(cartesian(plat, plon), norm))
      /sqrt(scalar_prod(norm, norm));
  
  if (scalar_prod_ > 1)
    scalar_prod_ = 1;
  
  return asin(scalar_prod_)*(10*1000*1000/acos(0));
}


bool intersect(const Prepared_Segment& segment_a,
               const Prepared_Segment& segment_b)
{
  vector< double > intersection_pt = cross_prod(segment_a.norm, segment_b.norm);
  rescale(1.0/sqrt(scalar_prod(intersection_pt, intersection_pt)), intersection_pt);
  
  vector< double > asum = sum(segment_a.first_cartesian, segment_a.second_cartesian);
  vector< double > bsum = sum(segment_b.first_cartesian, segment_b.second_cartesian);
  
  return (abs(scalar_prod(asum, intersection_pt)) >= scalar_prod(asum, segment_a.first_cartesian)
      && abs(scalar_prod(bsum, intersection_pt)) >= scalar_prod(bsum, segment_b.first_cartesian));
}


bool intersect(double alat1, double alon1, double alat2, double alon2,
	       double blat1, double blon1, double blat2, double blon2)
{
  vector< double > a1 = cartesian(alat1, alon1);
  vector< double > a2 = cartesian(alat2, alon2);
  vector< double > norm_a = cross_prod(a1, a2);
  vector< double > b1 = cartesian(blat1, blon1);
  vector< double > b2 = cartesian(blat2, blon2);
  vector< double > norm_b = cross_prod(b1, b2);
  
  vector< double > intersection_pt = cross_prod(norm_a, norm_b);
  rescale(1.0/sqrt(scalar_prod(intersection_pt, intersection_pt)), intersection_pt);
  
  vector< double > asum = sum(a1, a2);
  vector< double > bsum = sum(b1, b2);
  
  return (abs(scalar_prod(asum, intersection_pt)) >= scalar_prod(asum, a1)
      && abs(scalar_prod(bsum, intersection_pt)) >= scalar_prod(bsum, b1));
}


set< pair< Uint32_Index, Uint32_Index > > Around_Statement::calc_ranges
    (const Set& input, Resource_Manager& rman) const
{
  if (lat < 100.0)
    return expand(ranges(lat, lon), radius);
  else
    return expand(set_union_
        (ranges(input.nodes), children(set_union_(ranges(input.ways), ranges(input.relations)))),
        radius);
}


void add_coord(double lat, double lon, double radius,
	       map< Uint32_Index, vector< pair< double, double > > >& radius_lat_lons,
	       vector< Prepared_Point >& simple_lat_lons)
{
  double south = lat - radius*(360.0/(40000.0*1000.0));
  double north = lat + radius*(360.0/(40000.0*1000.0));
  double scale_lat = lat > 0.0 ? north : south;
  if (abs(scale_lat) >= 89.9)
    scale_lat = 89.9;
  double west = lon - radius*(360.0/(40000.0*1000.0))/cos(scale_lat/90.0*acos(0));
  double east = lon + radius*(360.0/(40000.0*1000.0))/cos(scale_lat/90.0*acos(0));
  
  simple_lat_lons.push_back(Prepared_Point(lat, lon));
  
  vector< pair< uint32, uint32 > > uint_ranges
      (calc_ranges(south, north, west, east));
  for (vector< pair< uint32, uint32 > >::const_iterator
      it(uint_ranges.begin()); it != uint_ranges.end(); ++it)
  {
    pair< Uint32_Index, Uint32_Index > range
        (make_pair(Uint32_Index(it->first), Uint32_Index(it->second)));
    
    for (uint32 idx = Uint32_Index(it->first).val();
        idx < Uint32_Index(it->second).val(); ++idx)
      radius_lat_lons[idx].push_back(make_pair(lat, lon));
  }
}


void add_node(Uint32_Index idx, const Node_Skeleton& node, double radius,
              map< Uint32_Index, vector< pair< double, double > > >& radius_lat_lons,
              vector< Prepared_Point >& simple_lat_lons)
{
  add_coord(::lat(idx.val(), node.ll_lower), ::lon(idx.val(), node.ll_lower),
            radius, radius_lat_lons, simple_lat_lons);
}


void add_way(const vector< Quad_Coord >& way_geometry, double radius,
             map< Uint32_Index, vector< pair< double, double > > >& radius_lat_lons,
             vector< Prepared_Point >& simple_lat_lons,
             vector< Prepared_Segment >& simple_segments)
{
  // add nodes
  
  for (vector< Quad_Coord >::const_iterator nit = way_geometry.begin(); nit != way_geometry.end(); ++nit)
    add_coord(::lat(nit->ll_upper, nit->ll_lower),
              ::lon(nit->ll_upper, nit->ll_lower),
              radius, radius_lat_lons, simple_lat_lons);
  
  // add segments
  
  vector< Quad_Coord >::const_iterator nit = way_geometry.begin();
  if (nit == way_geometry.end())
    return;
    
  double first_lat(::lat(nit->ll_upper, nit->ll_lower));
  double first_lon(::lon(nit->ll_upper, nit->ll_lower));
  
  for (++nit; nit != way_geometry.end(); ++nit)
  {
    double second_lat(::lat(nit->ll_upper, nit->ll_lower));
    double second_lon(::lon(nit->ll_upper, nit->ll_lower));
    
    simple_segments.push_back(Prepared_Segment(first_lat, first_lon, second_lat, second_lon));
                                        
    first_lat = second_lat;
    first_lon = second_lon;
  }
}


void Around_Statement::calc_lat_lons(const Set& input, Statement& query, Resource_Manager& rman)
{
  radius_lat_lons.clear();
  simple_lat_lons.clear();
  
  simple_segments.clear();
  
  if (lat < 100.0)
  {
    add_coord(lat, lon, radius, radius_lat_lons, simple_lat_lons);
    return;
  }
  
  for (map< Uint32_Index, vector< Node_Skeleton > >::const_iterator iit(input.nodes.begin());
      iit != input.nodes.end(); ++iit)
  {
    for (vector< Node_Skeleton >::const_iterator nit(iit->second.begin());
        nit != iit->second.end(); ++nit)
      add_node(iit->first, *nit, radius, radius_lat_lons, simple_lat_lons);
  }
  
  {
    //Process ways
    Way_Geometry_Store way_geometries(input.ways, query, rman);
    
    for (map< Uint31_Index, vector< Way_Skeleton > >::const_iterator it = input.ways.begin();
        it != input.ways.end(); ++it)
    {
      vector< Way_Skeleton > local_into;
      for (vector< Way_Skeleton >::const_iterator iit = it->second.begin();
          iit != it->second.end(); ++iit)
	add_way(way_geometries.get_geometry(*iit), radius,
                radius_lat_lons, simple_lat_lons, simple_segments);
    }
  }
  {
    //Process relations
    
    // Retrieve all node and way members referred by the relations.
    Relation_Member_Collection relation_members(input.relations, query, rman, 0, 0);
        
    // Retrieve all nodes referred by the ways.
    Way_Geometry_Store way_geometries(relation_members.way_members, query, rman);
    
    for (map< Uint31_Index, vector< Relation_Skeleton > >::const_iterator
        it = input.relations.begin(); it != input.relations.end(); ++it)
    {
      for (vector< Relation_Skeleton >::const_iterator iit = it->second.begin();
          iit != it->second.end(); ++iit)
      {
	for (vector< Relation_Entry >::const_iterator nit = iit->members.begin();
	    nit != iit->members.end(); ++nit)
        {
	  if (nit->type == Relation_Entry::NODE)
	  {
	    const pair< Uint32_Index, const Node_Skeleton* >* second_nd =
	        relation_members.get_node_by_id(nit->ref);
	    if (!second_nd)
	      continue;
	    
	    add_node(second_nd->first, *second_nd->second, radius, radius_lat_lons, simple_lat_lons);
	  }
	  else if (nit->type == Relation_Entry::WAY)
	  {
	    const pair< Uint31_Index, const Way_Skeleton* >* second_nd =
	        relation_members.get_way_by_id(nit->ref32());
	    if (!second_nd)
	      continue;
	    
	    add_way(way_geometries.get_geometry(*second_nd->second), radius,
                    radius_lat_lons, simple_lat_lons, simple_segments);
	  }
        }
      }
    }
  }  
}

void Around_Statement::forecast()
{
}

bool Around_Statement::is_inside(double lat, double lon) const
{
  map< Uint32_Index, vector< pair< double, double > > >::const_iterator mit
      = radius_lat_lons.find(::ll_upper_(lat, lon));
  if (mit != radius_lat_lons.end())
  {
    for (vector< pair< double, double > >::const_iterator cit = mit->second.begin();
        cit != mit->second.end(); ++cit)
    {
      if ((radius > 0 && great_circle_dist(cit->first, cit->second, lat, lon) <= radius)
          || (cit->first == lat && cit->second == lon))
        return true;
    }
  }
  
  vector< double > coord_cartesian = cartesian(lat, lon);
  for (vector< Prepared_Segment >::const_iterator
      it = simple_segments.begin(); it != simple_segments.end(); ++it)
  {
    if (great_circle_line_dist(*it, coord_cartesian) <= radius)
    {
      double gcdist = great_circle_dist
          (it->first_lat, it->first_lon, it->second_lat, it->second_lon);
      double limit = sqrt(gcdist*gcdist + radius*radius);
      if (great_circle_dist(lat, lon, it->first_lat, it->first_lon) <= limit &&
          great_circle_dist(lat, lon, it->second_lat, it->second_lon) <= limit)
	return true;
    }
  }
  
  return false;
}

bool Around_Statement::is_inside
    (double first_lat, double first_lon, double second_lat, double second_lon) const
{
  Prepared_Segment segment(first_lat, first_lon, second_lat, second_lon);
  
  for (vector< Prepared_Point >::const_iterator cit = simple_lat_lons.begin();
      cit != simple_lat_lons.end(); ++cit)
  {
    if (great_circle_line_dist(segment, cit->cartesian) <= radius)
    {
      double gcdist = great_circle_dist(first_lat, first_lon, second_lat, second_lon);
      double limit = sqrt(gcdist*gcdist + radius*radius);
      if (great_circle_dist(cit->lat, cit->lon, first_lat, first_lon) <= limit &&
	  great_circle_dist(cit->lat, cit->lon, second_lat, second_lon) <= limit)
        return true;
    }
  }
  
  for (vector< Prepared_Segment >::const_iterator
      cit = simple_segments.begin(); cit != simple_segments.end(); ++cit)
  {
    if (intersect(*cit, segment))
      return true;
  }
  
  return false;
}


bool Around_Statement::is_inside(const vector< Quad_Coord >& way_geometry) const
{
  vector< Quad_Coord >::const_iterator nit = way_geometry.begin();
  if (nit == way_geometry.end())
    return false;
  
  // Pre-check if a node is inside
  for (vector< Quad_Coord >::const_iterator it = way_geometry.begin(); it != way_geometry.end(); ++it)
  {
    double second_lat(::lat(it->ll_upper, it->ll_lower));
    double second_lon(::lon(it->ll_upper, it->ll_lower));

    if (is_inside(second_lat, second_lon))
      return true;
  }
  
  double first_lat(::lat(nit->ll_upper, nit->ll_lower));
  double first_lon(::lon(nit->ll_upper, nit->ll_lower));
  
  for (++nit; nit != way_geometry.end(); ++nit)
  {
    double second_lat(::lat(nit->ll_upper, nit->ll_lower));
    double second_lon(::lon(nit->ll_upper, nit->ll_lower));
    
    if (is_inside(first_lat, first_lon, second_lat, second_lon))
      return true;
    
    first_lat = second_lat;
    first_lon = second_lon;
  }
  return false;
}


void Around_Statement::execute(Resource_Manager& rman)
{
  set< pair< Uint32_Index, Uint32_Index > > req = calc_ranges(rman.sets()[input], rman);  
  calc_lat_lons(rman.sets()[input], *this, rman);

  map< Uint32_Index, vector< Node_Skeleton > >& nodes
      (rman.sets()[output].nodes);
      
  nodes.clear();
  rman.sets()[output].ways.clear();
  rman.sets()[output].relations.clear();
  rman.sets()[output].areas.clear();

  uint nodes_count = 0;
  
  Block_Backend< Uint32_Index, Node_Skeleton > nodes_db
    (rman.get_transaction()->data_index(osm_base_settings().NODES));
  for (Block_Backend< Uint32_Index, Node_Skeleton >::Range_Iterator
    it(nodes_db.range_begin
      (Default_Range_Iterator< Uint32_Index >(req.begin()),
       Default_Range_Iterator< Uint32_Index >(req.end())));
    !(it == nodes_db.range_end()); ++it)
  {
    if (++nodes_count >= 64*1024)
    {
      nodes_count = 0;
      rman.health_check(*this);
    }
    
    double lat(::lat(it.index().val(), it.object().ll_lower));
    double lon(::lon(it.index().val(), it.object().ll_lower));
    if (is_inside(lat, lon))
      nodes[it.index()].push_back(it.object());
  }

  rman.health_check(*this);
}

Query_Constraint* Around_Statement::get_query_constraint()
{
  constraints.push_back(new Around_Constraint(*this));
  return constraints.back();
}
