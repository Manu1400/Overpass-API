#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "statement.h"
#include "area_query.h"
#include "around.h"
#include "bbox_query.h"
#include "coord_query.h"
#include "foreach.h"
#include "id_query.h"
#include "item.h"
#include "make_area.h"
#include "newer.h"
#include "osm_script.h"
#include "print.h"
#include "query.h"
#include "recurse.h"
#include "union.h"
#include "user.h"

using namespace std;

void Statement::eval_attributes_array(string element, map< string, string >& attributes,
				      const map< string, string >& input)
{
  for (map< string, string >::const_iterator it = input.begin(); it != input.end(); ++it)
  {
    map< string, string >::iterator ait(attributes.find(it->first));
    if (ait != attributes.end())
      ait->second = it->second;
    else
    {
      ostringstream temp;
      temp<<"Unknown attribute \""<<it->first<<"\" in element \""<<element<<"\".";
      add_static_error(temp.str());
    }
  }
}

void Statement::assure_no_text(string text, string name)
{
  for (unsigned int i(0); i < text.size(); ++i)
  {
    if (!isspace(text[i]))
    {
      ostringstream temp;
      temp<<"Element \""<<name<<"\" must not contain text.";
      add_static_error(temp.str());
      break;
    }
  }
}

void Statement::substatement_error(string parent, Statement* child)
{
  ostringstream temp;
  temp<<"Element \""<<child->get_name()<<"\" cannot be subelement of element \""<<parent<<"\".";
  add_static_error(temp.str());
  
  delete child;
}

void Statement::add_statement(Statement* statement, string text)
{
  assure_no_text(text, this->get_name());
  substatement_error(get_name(), statement);
}

void Statement::add_final_text(string text)
{
  assure_no_text(text, this->get_name());
}

void Statement::display_full()
{
  //display_verbatim(get_source(startpos, endpos - startpos));
}

void Statement::display_starttag()
{
  //display_verbatim(get_source(startpos, tagendpos - startpos));
}

Statement* Statement::create_statement(string element, int line_number,
				       const map< string, string >& attributes)
{
  if (element == "area-query")
    return new Area_Query_Statement(line_number, attributes);
  else if (element == "around")
    return new Around_Statement(line_number, attributes);
  else if (element == "bbox-query")
    return new Bbox_Query_Statement(line_number, attributes);
/*  else if (element == "conflict")
    return new Conflict_Statement(line_number);*/
  else if (element == "coord-query")
    return new Coord_Query_Statement(line_number, attributes);
/*  else if (element == "detect-odd-nodes")
    return new Detect_Odd_Nodes_Statement();*/
  else if (element == "foreach")
    return new Foreach_Statement(line_number, attributes);
  else if (element == "has-kv")
    return new Has_Kv_Statement(line_number, attributes);
  else if (element == "id-query")
    return new Id_Query_Statement(line_number, attributes);
  else if (element == "item")
    return new Item_Statement(line_number, attributes);
  else if (element == "make-area")
    return new Make_Area_Statement(line_number, attributes);
  else if (element == "newer")
    return new Newer_Statement(line_number, attributes);
  else if (element == "osm-script")
    return new Osm_Script_Statement(line_number, attributes);
  else if (element == "print")
    return new Print_Statement(line_number, attributes);
  else if (element == "query")
    return new Query_Statement(line_number, attributes);
  else if (element == "recurse")
    return new Recurse_Statement(line_number, attributes);
/*  else if (element == "report")
    return new Report_Statement();*/
  else if (element == "union")
    return new Union_Statement(line_number, attributes);
  else if (element == "user")
    return new User_Statement(line_number, attributes);
  
  ostringstream temp;
  temp<<"Unknown tag \""<<element<<"\" in line "<<line_number<<'.';
  if (error_output)
    error_output->add_static_error(temp.str(), line_number);
  
  return 0;
}

Error_Output* Statement::error_output = 0;

void Statement::add_static_error(string error)
{
  if (error_output)
    error_output->add_static_error(error, line_number);
}

void Statement::add_static_remark(string remark)
{
  if (error_output)
    error_output->add_static_remark(remark, line_number);
}

void Statement::runtime_remark(string error)
{
  if (error_output)
    error_output->runtime_remark(error);
}

map< string, string > convert_c_pairs(const char** attr)
{
  map< string, string > result;
  for (int i = 0; attr[i]; i+=2)
    result[attr[i]] = attr[i+1];
  return result;
}