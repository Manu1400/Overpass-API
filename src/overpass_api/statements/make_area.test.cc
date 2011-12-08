#include <iostream>
#include <sstream>
#include "../../template_db/block_backend.h"
#include "../core/settings.h"
#include "../frontend/console_output.h"
#include "area_query.h"
#include "bbox_query.h"
#include "coord_query.h"
#include "foreach.h"
#include "id_query.h"
#include "item.h"
#include "make_area.h"
#include "print.h"
#include "query.h"
#include "recurse.h"
#include "union.h"

#include <iomanip>

using namespace std;

void evaluate_grid(double south, double north, double west, double east,
		   double step, Resource_Manager& rman)
{  
  bool areas_printed = false;
  vector< vector< uint > > area_counter;
  for (double dlat = 0; dlat < north - south + step/2; dlat += step)
  {
    area_counter.push_back(vector< uint >());
    for (double dlon = 0; dlon < east - west + step/2; dlon += step)
    {
      {
	ostringstream v_lat, v_lon;
	v_lat<<fixed<<setprecision(7)<<(dlat + south);
	v_lon<<fixed<<setprecision(7)<<(dlon + west);
	string s_lat = v_lat.str();
	string s_lon = v_lon.str();
	const char* attributes[] = { "lat", s_lat.c_str(), "lon", s_lon.c_str(), 0 };
	Coord_Query_Statement* stmt1 = new Coord_Query_Statement(0, convert_c_pairs(attributes));
        stmt1->execute(rman);
      }
      uint area_count = 0;
      for (map< Uint31_Index, vector< Area_Skeleton > >::const_iterator
	  it = rman.sets()["_"].areas.begin(); it != rman.sets()["_"].areas.end(); ++it)
	area_count += it->second.size();
      area_counter.back().push_back(area_count);
      if (!areas_printed && !rman.sets()["_"].areas.empty())
      {
	areas_printed = true;
	
	const char* attributes[] = { 0 };
	Print_Statement* stmt1 = new Print_Statement(0, convert_c_pairs(attributes));
	stmt1->execute(rman);
      }
    }
  }
  
  for (vector< vector< uint > >::const_reverse_iterator it = area_counter.rbegin();
      it != area_counter.rend(); ++it)
  {
    for (vector< uint >::const_iterator it2 = it->begin(); it2 != it->end(); ++it2)
      cout<<' '<<*it2;
    cout<<'\n';
  }
}

int main(int argc, char* args[])
{
  if (argc < 4)
  {
    cout<<"Usage: "<<args[0]<<" test_to_execute pattern_size db_dir\n";
    return 0;
  }
  string test_to_execute = args[1];
  uint pattern_size = 0;
  pattern_size = atoi(args[2]);
  string db_dir(args[3]);
  
  Error_Output* error_output(new Console_Output(Error_Output::ASSISTING));
  Statement::set_error_output(error_output);
  
  Nonsynced_Transaction transaction(false, false, db_dir, "");
  Nonsynced_Transaction area_transaction(true, false, db_dir, "");
  Resource_Manager rman(transaction, 0, area_transaction, 0, true);
  
  if (test_to_execute == "create")
  {
    {
      const char* attributes[] = { 0 };
      Union_Statement* stmt3 = new Union_Statement(0, convert_c_pairs(attributes));
      {
	const char* attributes[] = { "type", "way", 0 };
	Query_Statement* stmt1 = new Query_Statement(0, convert_c_pairs(attributes));
        {
	  const char* attributes[] = { "k", "triangle", 0 };
	  Has_Kv_Statement* stmt2 = new Has_Kv_Statement(0, convert_c_pairs(attributes));
          stmt1->add_statement(stmt2, "");
        }
	stmt3->add_statement(stmt1, "");
      }
      {
	const char* attributes[] = { "type", "way", 0 };
	Query_Statement* stmt1 = new Query_Statement(0, convert_c_pairs(attributes));
        {
	  const char* attributes[] = { "k", "shapes", 0 };
	  Has_Kv_Statement* stmt2 = new Has_Kv_Statement(0, convert_c_pairs(attributes));
          stmt1->add_statement(stmt2, "");
        }
	stmt3->add_statement(stmt1, "");
      }
      stmt3->execute(rman);
    }
    {
      const char* attributes[] = { "into", "way", 0 };
      Foreach_Statement* stmt1 = new Foreach_Statement(0, convert_c_pairs(attributes));
      {
	const char* attributes[] = { 0 };
	Union_Statement* stmt2 = new Union_Statement(0, convert_c_pairs(attributes));
        {
	  const char* attributes[] = { "type", "way-node", "from", "way", 0 };
	  Recurse_Statement* stmt3 = new Recurse_Statement(0, convert_c_pairs(attributes));
          stmt2->add_statement(stmt3, "");
        }
        {
	  const char* attributes[] = { "set", "way", 0 };
	  Item_Statement* stmt3 = new Item_Statement(0, convert_c_pairs(attributes));
          stmt2->add_statement(stmt3, "");
        }
        stmt1->add_statement(stmt2, "");
      }
      {
	const char* attributes[] = { "pivot", "way", 0 };
	Make_Area_Statement* stmt2 = new Make_Area_Statement(0, convert_c_pairs(attributes));
        stmt1->add_statement(stmt2, "");
      }
      stmt1->execute(rman);
    }

    {
      const char* attributes[] = { "type", "relation", 0 };
      Query_Statement* stmt1 = new Query_Statement(0, convert_c_pairs(attributes));
      {
	const char* attributes[] = { "k", "multpoly", 0 };
	Has_Kv_Statement* stmt2 = new Has_Kv_Statement(0, convert_c_pairs(attributes));
	stmt1->add_statement(stmt2, "");
      }
      stmt1->execute(rman);
    }
    {
      const char* attributes[] = { "into", "pivot", 0 };
      Foreach_Statement* stmt1 = new Foreach_Statement(0, convert_c_pairs(attributes));
      {
	const char* attributes[] = { 0 };
	Union_Statement* stmt2 = new Union_Statement(0, convert_c_pairs(attributes));
        {
	  const char* attributes[] = { "type", "relation-way", "from", "pivot", 0 };
	  Recurse_Statement* stmt3 = new Recurse_Statement(0, convert_c_pairs(attributes));
          stmt2->add_statement(stmt3, "");
        }
        {
	  const char* attributes[] = { "type", "way-node", 0 };
	  Recurse_Statement* stmt3 = new Recurse_Statement(0, convert_c_pairs(attributes));
          stmt2->add_statement(stmt3, "");
        }
        stmt1->add_statement(stmt2, "");
      }
      {
	const char* attributes[] = { "pivot", "pivot", 0 };
	Make_Area_Statement* stmt2 = new Make_Area_Statement(0, convert_c_pairs(attributes));
        stmt1->add_statement(stmt2, "");
      }
      stmt1->execute(rman);
    }
  }
  else if (test_to_execute == "1")
  {
    for (uint i = 0; i < 8; ++i)
      evaluate_grid( 0.0, 10.0, 0.0 + 6.0*i, 6.0 + 6.0*i, 1.0, rman);
    for (uint i = 0; i < 8; ++i)
      evaluate_grid(10.0, 11.0, 0.0 + 0.6*i, 0.6 + 0.6*i, 0.1, rman);
    for (uint i = 0; i < 8; ++i)
      evaluate_grid(11.0, 11.1, 0.0 + 0.06*i, 0.06 + 0.06*i, 0.01, rman);
    for (uint i = 0; i < 8; ++i)
      evaluate_grid(11.1, 11.11, 0.0 + 0.006*i, 0.006 + 0.006*i, 0.001, rman);
    for (uint i = 0; i < 8; ++i)
      evaluate_grid(11.11, 11.111, 0.0 + 0.0006*i, 0.0006 + 0.0006*i, 0.0001, rman);
    for (uint i = 0; i < 8; ++i)
      evaluate_grid(11.111, 11.1111, 0.0 + 0.00006*i, 0.00006 + 0.00006*i, 0.00001, rman);
    for (uint i = 0; i < 8; ++i)
      evaluate_grid(11.1111, 11.11111, 0.0 + 0.000006*i, 0.000006 + 0.000006*i, 0.000001, rman);
  }
  else if (test_to_execute == "2")
  {
    evaluate_grid(0.0, 5.0, 50.0, 55.0, 1.0, rman);
    evaluate_grid(0.0, 3.0, 55.0, 60.0, 1.0, rman);
    evaluate_grid(10.0, 10.5, 10.0, 10.5, 0.1, rman);
    evaluate_grid(10.0, 10.3, 10.5, 11.0, 0.1, rman);
    evaluate_grid(11.0, 11.05, 10.0, 10.05, 0.01, rman);
    evaluate_grid(11.0, 11.03, 10.05, 10.1, 0.01, rman);
    evaluate_grid(11.1, 11.105, 10.0, 10.005, 0.001, rman);
    evaluate_grid(11.1, 11.103, 10.005, 10.01, 0.001, rman);
    evaluate_grid(11.11, 11.1105, 10.0, 10.0005, 0.0001, rman);
    evaluate_grid(11.11, 11.1103, 10.0005, 10.001, 0.0001, rman);
    evaluate_grid(11.111, 11.11105, 10.0, 10.00005, 0.00001, rman);
    evaluate_grid(11.111, 11.11103, 10.00005, 10.0001, 0.00001, rman);
    evaluate_grid(11.1111, 11.111105, 10.0, 10.000005, 0.000001, rman);
    evaluate_grid(11.1111, 11.111103, 10.000005, 10.00001, 0.000001, rman);
  }
  else if (test_to_execute == "3")
  {
    evaluate_grid(10.0, 10.3, 20.0, 20.5, 0.05, rman);
    evaluate_grid(10.0, 10.4, 20.5, 20.9, 0.05, rman);
    evaluate_grid(10.3, 10.3, 20.6, 20.6, 0.05, rman);
    evaluate_grid(10.1, 10.1, 20.8, 20.8, 0.05, rman);
  }
  else if (test_to_execute == "4")
  {
    {
      const char* attributes[] = { "ref", "2400000121", 0 };
      Area_Query_Statement* stmt1 = new Area_Query_Statement(0, convert_c_pairs(attributes));
      stmt1->execute(rman);
    }
    {
      const char* attributes[] = { 0 };
      Print_Statement* stmt1 = new Print_Statement(0, convert_c_pairs(attributes));
      stmt1->execute(rman);
    }
  }
  
  return 0;
}
