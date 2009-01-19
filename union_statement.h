#ifndef UNION_STATEMENT_DEFINED
#define UNION_STATEMENT_DEFINED

#include <map>
#include <string>
#include <vector>
#include "script_datatypes.h"

#include <mysql.h>

using namespace std;

class Union_Statement : public Statement
{
  public:
    Union_Statement() {}
    virtual void set_attributes(const char **attr);
    virtual void add_statement(Statement* statement, string text);
    virtual string get_name() { return "union"; }
    virtual string get_result_name() { return output; }
    virtual void execute(MYSQL* mysql, map< string, Set >& maps);
    virtual ~Union_Statement() {}
    
  private:
    string output;
    vector< Statement* > substatements;
};

#endif