/**
 * The Seeks proxy and plugin framework are part of the SEEKS project.
 * Copyright (C) 2010 Emmanuel Benazera, juban@free.fr
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "json_renderer.h"
#include "cgisimple.h"
#include "miscutil.h"
#include "cgi.h"

using sp::cgisimple;
using sp::miscutil;
using sp::cgi;

namespace seeks_plugins
{
   sp_err json_renderer::render_snippets(const int &current_page,
					 const std::vector<search_snippet*> &snippets,
					 std::string &json_str,
					 const hash_map<const char*, const char*, hash<const char*>, eqstr> *parameters)
     {
	json_str += "\"snippets\":[";
	bool has_thumbs = false;
	const char *thumbs = miscutil::lookup(parameters,"thumbs");
	if (thumbs && strcmp(thumbs,"on")==0)
	  has_thumbs = true;
	
	if (!snippets.empty())
	  {
	     // check whether we're rendering similarity snippets.
	     bool similarity = false;
	     if (snippets.at(0)->_seeks_ir > 0)
	       similarity = true;
	  
	     // proceed with rendering.
	     const char *rpp_str = miscutil::lookup(parameters,"rpp"); // results per page.
	     int rpp = websearch::_wconfig->_Nr;
	     if (rpp_str)
	       rpp = atoi(rpp_str);
	     size_t snisize = snippets.size();
	     size_t snistart = 0;
	     if (current_page == 0) // page is not taken into account.
	       {
	       }
	     else
	       {
		  snisize = std::min(current_page*rpp,(int)snippets.size());
		  snistart = (current_page-1)*rpp;
	       }
	     for (size_t i=snistart;i<snisize;i++)
	       {
		  if (snippets.at(i)->_doc_type == REJECTED)
		    continue;
		  if (!similarity || snippets.at(i)->_seeks_ir > 0)
		    json_str += snippets.at(i)->to_json(has_thumbs);
		  if (i!=snisize-1)
		    json_str += ",";
	       }
	  }
	json_str += "]";
	return SP_ERR_OK;
     }

   sp_err json_renderer::render_clustered_snippets(cluster *clusters, const short &K,
						   const query_context *qc,
						   std::string &json_str,
						   const hash_map<const char*,const char*,hash<const char*>,eqstr> *parameters)
     {
	json_str += "\"clusters\":[";
	
	// render every cluster and snippets within.
	for (short c=0;c<K;c++)
	  {
	     if (clusters[c]._cpoints.empty())
	       continue;
	     
	     std::vector<search_snippet*> snippets;
	     snippets.reserve(clusters[c]._cpoints.size());
	     hash_map<uint32_t,hash_map<uint32_t,float,id_hash_uint>*,id_hash_uint>::const_iterator hit
	       = clusters[c]._cpoints.begin();
	     while(hit!=clusters[c]._cpoints.end())
	       {
		  search_snippet *sp = qc->get_cached_snippet((*hit).first);
		  snippets.push_back(sp);
		  ++hit;
	       }
	     std::stable_sort(snippets.begin(),snippets.end(),search_snippet::max_seeks_ir);
	     
	     json_str += "{";
	     json_str += "\"label\":\"" + clusters[c]._label + "\",";	     
	     json_renderer::render_snippets(0,snippets,json_str,parameters);
	     json_str += "},";
	  }
	
	json_str += "]";
	return SP_ERR_OK;
     }
   
   sp_err json_renderer::render_json_results(const std::vector<search_snippet*> &snippets,
					     client_state *csp, http_response *rsp,
					     const hash_map<const char*, const char*, hash<const char*>, eqstr> *parameters,
					     const query_context *qc,
					     const double &qtime)
     {
	std::string json_str = "{";
	
	const char *current_page_str = miscutil::lookup(parameters,"page");
	if (!current_page_str)
	  {
	     //XXX: no page argument, we default to first page.
	     current_page_str = "1";
	  }
	int current_page = atoi(current_page_str);
	
	//TODO: other parameters.
	
	// search snippets.
	sp_err err = json_renderer::render_snippets(current_page,snippets,json_str,parameters);
	
	// render date & exec time.
	char datebuf[256];
	cgi::get_http_time(0,datebuf,sizeof(datebuf));
	json_str += ",\"date\":\"" + std::string(datebuf) + "\"";
	json_str += ",\"qtime\":" + miscutil::to_string(qtime);
	json_str += "}";
	
	// fill up the response body.
	rsp->_body = strdup(json_str.c_str());
	rsp->_content_length = json_str.size();
	miscutil::enlist(&rsp->_headers, "Content-Type: application/json");
	rsp->_is_static = 1;
	
	return SP_ERR_OK;
     }

   sp_err json_renderer::render_clustered_json_results(cluster *clusters,
						       const short &K,
						       client_state *csp, http_response *rsp,
						       const hash_map<const char*, const char*, hash<const char*>, eqstr> *parameters,
						       const query_context *qc,
						       const double &qtime)
     {
	std::string json_str = "{";
	
	// clustered snippets.
	sp_err err = json_renderer::render_clustered_snippets(clusters,K,qc,json_str,parameters);
	
	// render date & exec time.
	char datebuf[256];
	cgi::get_http_time(0,datebuf,sizeof(datebuf));
	json_str += ",\"date\":\"" + std::string(datebuf) + "\"";
	json_str += ",\"qtime\":" + miscutil::to_string(qtime);
	json_str += "}";
	
	// fill up the response body.
	rsp->_body = strdup(json_str.c_str());
	rsp->_content_length = json_str.size();
	miscutil::enlist(&rsp->_headers, "Content-Type: application/json");
	rsp->_is_static = 1;
	
	return SP_ERR_OK;
     }
   
} /* end of namespace. */
