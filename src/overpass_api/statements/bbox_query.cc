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

#include <algorithm>
#include <sstream>

#include "../../template_db/block_backend.h"
#include "../../template_db/random_file.h"
#include "../core/settings.h"
#include "../data/collect_members.h"
#include "../data/geometry.h"
#include "bbox_query.h"
#include "recurse.h"

using namespace std;

//-----------------------------------------------------------------------------

class Bbox_Constraint : public Query_Constraint
{
  public:
    bool delivers_data();
    
    Bbox_Constraint(Bbox_Query_Statement& bbox_) : bbox(&bbox_) {}
    bool get_ranges
        (Resource_Manager& rman, set< pair< Uint32_Index, Uint32_Index > >& ranges);
    bool get_ranges
        (Resource_Manager& rman, set< pair< Uint31_Index, Uint31_Index > >& ranges);
    void filter(Resource_Manager& rman, Set& into);
    void filter(const Statement& query, Resource_Manager& rman, Set& into);
    virtual ~Bbox_Constraint() {}
    
  private:
    Bbox_Query_Statement* bbox;
};


bool Bbox_Constraint::delivers_data()
{
  if (!bbox)
    return false;
  
  return ((bbox->get_north() - bbox->get_south())*(bbox->get_east() - bbox->get_west())
      < 1.0);
}


bool Bbox_Constraint::get_ranges
    (Resource_Manager& rman, set< pair< Uint32_Index, Uint32_Index > >& ranges)
{
  vector< pair< uint32, uint32 > > int_ranges = bbox->calc_ranges();
  for (vector< pair< uint32, uint32 > >::const_iterator
      it(int_ranges.begin()); it != int_ranges.end(); ++it)
  {
    pair< Uint32_Index, Uint32_Index > range
        (make_pair(Uint32_Index(it->first), Uint32_Index(it->second)));
    ranges.insert(range);
  }
  return true;
}

bool Bbox_Constraint::get_ranges
    (Resource_Manager& rman, set< pair< Uint31_Index, Uint31_Index > >& ranges)
{
  set< pair< Uint32_Index, Uint32_Index > > node_ranges;
  this->get_ranges(rman, node_ranges);
  ranges = calc_parents(node_ranges);
  return true;
}

void Bbox_Constraint::filter(Resource_Manager& rman, Set& into)
{
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
      if ((lat >= bbox->get_south()) && (lat <= bbox->get_north()) &&
	  (((lon >= bbox->get_west()) && (lon <= bbox->get_east())) ||
	  ((bbox->get_east() < bbox->get_west()) && ((lon >= bbox->get_west()) ||
	  (lon <= bbox->get_east())))))
	local_into.push_back(*iit);
    }
    it->second.swap(local_into);
  }  

  
  set< pair< Uint31_Index, Uint31_Index > > ranges;
  get_ranges(rman, ranges);
  
  // pre-process ways to reduce the load of the expensive filter
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
  
  //TODO: filter areas
}


bool matches_bbox(const Bbox_Query_Statement& bbox, const vector< Quad_Coord >& way_geometry)
{
  vector< Quad_Coord >::const_iterator nit = way_geometry.begin();
  if (nit == way_geometry.end())
    return false;
  double first_lat(::lat(nit->ll_upper, nit->ll_lower));
  double first_lon(::lon(nit->ll_upper, nit->ll_lower));
  for (++nit; nit != way_geometry.end(); ++nit)
  {
    double second_lat(::lat(nit->ll_upper, nit->ll_lower));
    double second_lon(::lon(nit->ll_upper, nit->ll_lower));
    
    if (segment_intersects_bbox
        (first_lat, first_lon, second_lat, second_lon,
         bbox.get_south(), bbox.get_north(), bbox.get_west(), bbox.get_east()))
      return true;
    
    first_lat = second_lat;
    first_lon = second_lon;
  }
  return false;
}


void Bbox_Constraint::filter(const Statement& query, Resource_Manager& rman, Set& into)
{
  {
    //Process ways
    Way_Geometry_Store way_geometries(into.ways, query, rman);
  
    for (map< Uint31_Index, vector< Way_Skeleton > >::iterator it = into.ways.begin();
        it != into.ways.end(); ++it)
    {
      vector< Way_Skeleton > local_into;
      for (vector< Way_Skeleton >::const_iterator iit = it->second.begin();
          iit != it->second.end(); ++iit)
      {
        if (matches_bbox(*bbox, way_geometries.get_geometry(*iit)))
	  local_into.push_back(*iit);
      }
      it->second.swap(local_into);
    }
  }
  {
    //Process relations
    
    // Retrieve all nodes referred by the relations.
    set< pair< Uint32_Index, Uint32_Index > > node_ranges;
    get_ranges(rman, node_ranges);
    
    map< Uint32_Index, vector< Node_Skeleton > > node_members
        = relation_node_members(&query, rman, into.relations, &node_ranges);
    //collect_nodes(query, rman, into.relations.begin(), into.relations.end(), node_members,
    //		  node_ranges);
  
    // Order node ids by id.
    vector< pair< Uint32_Index, const Node_Skeleton* > > node_members_by_id;
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
    set< pair< Uint31_Index, Uint31_Index > > way_ranges;
    get_ranges(rman, way_ranges);
    
    map< Uint31_Index, vector< Way_Skeleton > > way_members_
        = relation_way_members(&query, rman, into.relations, &way_ranges);
/*    collect_ways(query, rman, into.relations.begin(), into.relations.end(), way_members_,
		 way_ranges);*/
    
    // Order way ids by id.
    vector< pair< Uint31_Index, const Way_Skeleton* > > way_members_by_id;
    for (map< Uint31_Index, vector< Way_Skeleton > >::iterator it = way_members_.begin();
        it != way_members_.end(); ++it)
    {
      for (vector< Way_Skeleton >::const_iterator iit = it->second.begin();
          iit != it->second.end(); ++iit)
        way_members_by_id.push_back(make_pair(it->first, &*iit));
    }
    Order_By_Way_Id order_by_way_id;
    sort(way_members_by_id.begin(), way_members_by_id.end(), order_by_way_id);
    
    Way_Geometry_Store way_geometries(way_members_, query, rman);
    
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
	        binary_search_for_pair_id(node_members_by_id, nit->ref);
	    if (!second_nd)
	      continue;
	    double lat(::lat(second_nd->first.val(), second_nd->second->ll_lower));
	    double lon(::lon(second_nd->first.val(), second_nd->second->ll_lower));
	  
	    if ((lat >= bbox->get_south()) && (lat <= bbox->get_north()) &&
	        (((lon >= bbox->get_west()) && (lon <= bbox->get_east())) ||
	        ((bbox->get_east() < bbox->get_west()) && ((lon >= bbox->get_west()) ||
	        (lon <= bbox->get_east())))))
	    {
	      local_into.push_back(*iit);
	      break;
	    }
	  }
	  else if (nit->type == Relation_Entry::WAY)
	  {
	    const pair< Uint31_Index, const Way_Skeleton* >* second_nd =
	        binary_search_for_pair_id(way_members_by_id, nit->ref32());
	    if (!second_nd)
	      continue;
            if (matches_bbox(*bbox, way_geometries.get_geometry(*second_nd->second)))
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
  
  //TODO: filter areas
}

//-----------------------------------------------------------------------------

Generic_Statement_Maker< Bbox_Query_Statement > Bbox_Query_Statement::statement_maker("bbox-query");

Bbox_Query_Statement::Bbox_Query_Statement
    (int line_number_, const map< string, string >& input_attributes)
    : Statement(line_number_)
{
  map< string, string > attributes;
  
  attributes["into"] = "_";
  attributes["s"] = "";
  attributes["n"] = "";
  attributes["w"] = "";
  attributes["e"] = "";
  
  eval_attributes_array(get_name(), attributes, input_attributes);
  
  output = attributes["into"];
  south = atof(attributes["s"].c_str());
  if ((south < -90.0) || (south > 90.0) || (attributes["s"] == ""))
  {
    ostringstream temp;
    temp<<"For the attribute \"s\" of the element \"bbox-query\""
    <<" the only allowed values are floats between -90.0 and 90.0.";
    add_static_error(temp.str());
  }
  north = atof(attributes["n"].c_str());
  if ((north < -90.0) || (north > 90.0) || (attributes["n"] == ""))
  {
    ostringstream temp;
    temp<<"For the attribute \"n\" of the element \"bbox-query\""
    <<" the only allowed values are floats between -90.0 and 90.0.";
    add_static_error(temp.str());
  }
  if (north < south)
  {
    ostringstream temp;
    temp<<"The value of attribute \"n\" of the element \"bbox-query\""
    <<" must always be greater or equal than the value of attribute \"s\".";
    add_static_error(temp.str());
  }
  west = atof(attributes["w"].c_str());
  if ((west < -180.0) || (west > 180.0) || (attributes["w"] == ""))
  {
    ostringstream temp;
    temp<<"For the attribute \"w\" of the element \"bbox-query\""
    <<" the only allowed values are floats between -180.0 and 180.0.";
    add_static_error(temp.str());
  }
  east = atof(attributes["e"].c_str());
  if ((east < -180.0) || (east > 180.0) || (attributes["e"] == ""))
  {
    ostringstream temp;
    temp<<"For the attribute \"e\" of the element \"bbox-query\""
    <<" the only allowed values are floats between -180.0 and 180.0.";
    add_static_error(temp.str());
  }
}

Bbox_Query_Statement::~Bbox_Query_Statement()
{
  for (vector< Query_Constraint* >::const_iterator it = constraints.begin();
      it != constraints.end(); ++it)
    delete *it;
}

void Bbox_Query_Statement::forecast()
{
}

void Bbox_Query_Statement::execute(Resource_Manager& rman)
{
  map< Uint32_Index, vector< Node_Skeleton > >& nodes
      (rman.sets()[output].nodes);
  map< Uint31_Index, vector< Way_Skeleton > >& ways
      (rman.sets()[output].ways);
  map< Uint31_Index, vector< Relation_Skeleton > >& relations
      (rman.sets()[output].relations);
  map< Uint31_Index, vector< Area_Skeleton > >& areas
      (rman.sets()[output].areas);
  
  nodes.clear();
  ways.clear();
  relations.clear();
  areas.clear();

  vector< pair< uint32, uint32 > > uint_ranges
    (::calc_ranges(south, north, west, east));
  rman.health_check(*this);
    
  set< pair< Uint32_Index, Uint32_Index > > req;
  for (vector< pair< uint32, uint32 > >::const_iterator
      it(uint_ranges.begin()); it != uint_ranges.end(); ++it)
  {
    pair< Uint32_Index, Uint32_Index > range
      (make_pair(Uint32_Index(it->first), Uint32_Index(it->second)));
    req.insert(range);
  }
  rman.health_check(*this);
  
  uint nodes_count = 0;
  
  uint32 isouth((south + 91.0)*10000000+0.5);
  uint32 inorth((north + 91.0)*10000000+0.5);
  int32 iwest(west*10000000 + (west > 0 ? 0.5 : -0.5));
  int32 ieast(east*10000000 + (east > 0 ? 0.5 : -0.5));
  
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
    
    uint32 ilat(::ilat(it.index().val(), it.object().ll_lower));
    int32 ilon(::ilon(it.index().val(), it.object().ll_lower));
    if ((ilat >= isouth) && (ilat <= inorth) &&
        (((ilon >= iwest) && (ilon <= ieast))
	  || ((ieast < iwest) && ((ilon >= iwest) || (ilon <= ieast)))))
      nodes[it.index()].push_back(it.object());    
  }
  
  rman.health_check(*this);
}

Query_Constraint* Bbox_Query_Statement::get_query_constraint()
{
  constraints.push_back(new Bbox_Constraint(*this));
  return constraints.back();
}
