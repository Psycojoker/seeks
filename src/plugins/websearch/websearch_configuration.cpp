/**
 * The Seeks proxy and plugin framework are part of the SEEKS project.
 * Copyright (C) 2009 Emmanuel Benazera, juban@free.fr
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 */

#include "websearch_configuration.h"

#include <iostream>

namespace seeks_plugins
{

#define hash_lang                   1526909359ul /* "search-langage" */
#define hash_n                       578814699ul /* "search-results-page" */  
#define hash_se                     1635576913ul /* "search-engine" */
#define hash_qcd                    4118649627ul /* "query-context-delay" */
   
   websearch_configuration::websearch_configuration(const std::string &filename)
     :configuration_spec(filename)
       {
	  _se_enabled = std::bitset<NSEs>(0);
	  load_config();
       }
   
   websearch_configuration::~websearch_configuration()
     {
     }
   
   void websearch_configuration::set_default_config()
     {
	_lang = "en";
	_N = 10;
	_se_enabled = std::bitset<NSEs>(000); // ggle only, TODO: change to all...
	_query_context_delay = 300; // in seconds, 5 minutes.
     }
   
   void websearch_configuration::handle_config_cmd(char *cmd, const uint32_t &cmd_hash, char *arg,
						   char *buf, const unsigned long &linenum)
     {
	switch(cmd_hash)
	  {
	   case hash_lang :
	     _lang = std::string(arg);
	     configuration_spec::html_table_row(_config_args,cmd,arg,
						"Websearch language");
	     break;
	     
	   case hash_n :
	       _N = atoi(arg);
	     configuration_spec::html_table_row(_config_args,cmd,arg,
						"Number of websearch results per page");
	     break;
	     
	   case hash_se :
	     if (strcasecmp(arg,"google") == 0)
	       _se_enabled |= std::bitset<NSEs>(SE_GOOGLE);
	     else if (strcasecmp(arg,"cuil") == 0)
	       _se_enabled |= std::bitset<NSEs>(SE_CUIL);
	     else if (strcasecmp(arg,"bing") == 0)
	       _se_enabled |= std::bitset<NSEs>(SE_BING);
	     configuration_spec::html_table_row(_config_args,cmd,arg,
						"Enabled search engine");
	     break;
	   case hash_qcd :
	     _query_context_delay = strtod(arg,NULL);
	     configuration_spec::html_table_row(_config_args,cmd,arg,
						"Delay in seconds before deletion of cached websearches and results");
	     break;
	   default :
	     break;
	     
	  } // end of switch.
     }
   
   void websearch_configuration::finalize_configuration()
     {
	// TODO.
     }
   
} /* end of namespace. */