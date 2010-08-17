/**
 * The Seeks proxy and plugin framework are part of the SEEKS project.
 * Copyright (C) 2009 Emmanuel Benazera, juban@free.fr
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

#include "static_renderer.h"
#include "mem_utils.h"
#include "plugin_manager.h"
#include "cgi.h"
#include "cgisimple.h"
#include "miscutil.h"
#include "encode.h"
#include "urlmatch.h"
#include "proxy_configuration.h"
#include "seeks_proxy.h"
#include "errlog.h"

#include <assert.h>
#include <ctype.h>
#include <iostream>
#include <algorithm>

using sp::miscutil;
using sp::cgi;
using sp::cgisimple;
using sp::plugin_manager;
using sp::cgi_dispatcher;
using sp::encode;
using sp::urlmatch;
using sp::proxy_configuration;
using sp::seeks_proxy;
using sp::errlog;
using lsh::LSHUniformHashTable;
using lsh::Bucket;

namespace seeks_plugins
{
   void static_renderer::render_query(const hash_map<const char*, const char*, hash<const char*>, eqstr> *parameters,
				      hash_map<const char*,const char*,hash<const char*>,eqstr> *exports,
				      std::string &html_encoded_query,
				      std::string &url_encoded_query)
     {
	const char *q = miscutil::lookup(parameters,"q");
	char *html_encoded_query_str = encode::html_encode(q);
	html_encoded_query = std::string(html_encoded_query_str);
	free(html_encoded_query_str);
	char *url_encoded_query_str = encode::url_encode(q);
	miscutil::add_map_entry(exports,"$fullquery",1,url_encoded_query_str,1);
	url_encoded_query = std::string(url_encoded_query_str);
	free(url_encoded_query_str);
     }
      
   void static_renderer::render_clean_query(const std::string &html_encoded_query,
					    hash_map<const char*,const char*,hash<const char*>,eqstr> *exports,
					    std::string &query_clean)
     {
	query_clean = se_handler::no_command_query(html_encoded_query);
	miscutil::add_map_entry(exports,"$qclean",1,query_clean.c_str(),1);
     }
   
   void static_renderer::render_suggestions(const query_context *qc,
					    hash_map<const char*,const char*,hash<const char*>,eqstr> *exports,
					    const std::string &cgi_base)
     {
	if (!qc->_suggestions.empty())
	  {
	     const char *base_url = miscutil::lookup(exports,"base-url");
	     std::string base_url_str = "";
	     if (base_url)
	       base_url_str = std::string(base_url);
	     std::string suggestion_str = "Suggestion:&nbsp;<a href=\"";
	     // for now, let's grab the first suggestion only.
	     std::string suggested_q_str = qc->_suggestions[0];
	     const char *sugg_enc = encode::html_encode(suggested_q_str.c_str());
	     std::string sugg_enc_str = std::string(sugg_enc);
	     free_const(sugg_enc);
	     suggestion_str += base_url_str + cgi_base + "q=" + qc->_in_query_command + " " 
	       + sugg_enc_str + "&amp;expansion=1&amp;action=expand";
	     suggestion_str += "\">";
	     suggestion_str += sugg_enc_str;
	     suggestion_str += "</a>";
	     miscutil::add_map_entry(exports,"$xxsugg",1,suggestion_str.c_str(),1);
	  }
	else miscutil::add_map_entry(exports,"$xxsugg",1,strdup(""),0);
     }
   
   void static_renderer::render_lang(const query_context *qc,
				     hash_map<const char*,const char*,hash<const char*>,eqstr> *exports)
     {
	miscutil::add_map_entry(exports,"$xxlang",1,qc->_auto_lang.c_str(),1);
     }
      
   void static_renderer::render_engines(const hash_map<const char*,const char*,hash<const char*>,eqstr> *parameters,
					hash_map<const char*,const char*,hash<const char*>,eqstr> *exports,
					std::string &engines)
     {
	const char *eng = miscutil::lookup(parameters,"engines");
	if (eng)
	  {
	     engines = std::string(eng);
	     miscutil::add_map_entry(exports,"$xxeng",1,eng,1);
	  }
	else 
	  {
	     engines = "";
	     miscutil::add_map_entry(exports,"$xxeng",1,strdup(""),0);
	  }
     }
         
   void static_renderer::render_snippets(const std::string &query_clean,
					 const int &current_page,
					 const std::vector<search_snippet*> &snippets,
					 const hash_map<const char*,const char*,hash<const char*>,eqstr> *parameters,
					 hash_map<const char*,const char*,hash<const char*>,eqstr> *exports)
     {
	const char *base_url = miscutil::lookup(exports,"base-url");
	std::string base_url_str = "";
	if (base_url)
	  base_url_str = std::string(base_url);
	
	std::vector<std::string> words;
	miscutil::tokenize(query_clean,words," "); // tokenize query before highlighting keywords.
	
	cgi::map_block_killer(exports,"have-clustered-results-head");
	cgi::map_block_killer(exports,"have-clustered-results-body");
	
	int rpp = websearch::_wconfig->_Nr;
	const char *rpp_str = miscutil::lookup(parameters,"rpp");
	if (rpp_str)
	  rpp = atoi(rpp_str);
		
	// checks for safe snippets (for now, only used for images).
	const char* safesearch_p = miscutil::lookup(parameters,"safesearch");
	bool safesearch_off = false;
	if (safesearch_p)
	  safesearch_off = strcasecmp(safesearch_p,"on") == 0 ? false : true;
		
	std::string snippets_str;
	if (!snippets.empty())
	  {
	     // check whether we're rendering similarity snippets.
	     bool similarity = false;
	     if (snippets.at(0)->_seeks_ir > 0)
	       similarity = true;
	     	      
	     // proceed with rendering.
	     int ssize = snippets.size();
	     size_t snisize = std::min(current_page*rpp,(int)ssize);
	     size_t snistart = (current_page-1) * rpp;
	     size_t count = snistart;
	     	     
	     for (int i=snistart;i<ssize;i++)
	       {
		  if (snippets.at(i)->_doc_type == REJECTED)
		    continue;
		  if (!snippets.at(i)->is_se_enabled(parameters))
		    continue;
		  if (!safesearch_off && !snippets.at(i)->_safe)
		    continue;
		    
		  if (!similarity || snippets.at(i)->_seeks_ir > 0)
		    snippets_str += snippets.at(i)->to_html_with_highlight(words,base_url_str);
		  count++;
		  if (count == snisize)
		    break; // end list here.
	       }
	  }
	miscutil::add_map_entry(exports,"search_snippets",1,snippets_str.c_str(),1);
	if (rpp_str)
	  miscutil::add_map_entry(exports,"$xxrpp",1,rpp_str,1);
	else miscutil::add_map_entry(exports,"$xxrpp",1,strdup(""),0);
	miscutil::add_map_entry(exports,"$xxtrpp",1,
				miscutil::to_string(websearch::_wconfig->_Nr).c_str(),1);
     }
   
   void static_renderer::render_clustered_snippets(const std::string &query_clean,
						   const std::string &url_encoded_query,
						   const int &current_page,
						   cluster *clusters,
						   const short &K,
						   const query_context *qc,
						   const hash_map<const char*,const char*,hash<const char*>,eqstr> *parameters,
						   hash_map<const char*,const char*,hash<const char*>,eqstr> *exports)
     {
	static short template_K=7; // max number of clusters available in the template.
	
	const char *base_url = miscutil::lookup(exports,"base-url");
	std::string base_url_str = "";
	if (base_url)
	  base_url_str = std::string(base_url);
	
	std::vector<std::string> words;
	miscutil::tokenize(query_clean,words," "); // tokenize query before highlighting keywords.
		
	// check for empty cluster, and determine which rendering to use.
	short k = 0;
	for (short c=0;c<K;c++)
	  {
	     if (!clusters[c]._cpoints.empty())
	       k++;
	     if (k>2)
	       break;
	  }
		     
	std::string rplcnt = "ccluster";
	cgi::map_block_killer(exports,"have-one-column-results-head");
	if (k>1)
	  cgi::map_block_killer(exports,"have-one-column-results");
	else 
	  {
	     cgi::map_block_killer(exports,"have-clustered-results-body");
	     rplcnt = "search_snippets";
	  }
		
	// lookup activated engines.
	std::bitset<NSEs> se_enabled;
	query_context::fillup_engines(parameters,se_enabled);
	
	bool clusterize = false;
	const char *action_str = miscutil::lookup(parameters,"action");
	if (action_str && strcmp(action_str,"clusterize")==0)
	  clusterize = true;
	
	// renders every cluster and snippets within.
	int l = 0;
	for (short c=0;c<K;c++)
	  {
	     if (clusters[c]._cpoints.empty())
	       {
		  continue;
	       }
	     
	     std::vector<search_snippet*> snippets;
	     snippets.reserve(clusters[c]._cpoints.size());
	     hash_map<uint32_t,hash_map<uint32_t,float,id_hash_uint>*,id_hash_uint>::const_iterator hit 
	       = clusters[c]._cpoints.begin();
	     while(hit!=clusters[c]._cpoints.end())
	       {
		  search_snippet *sp = qc->get_cached_snippet((*hit).first);
		  if (sp->_doc_type == REJECTED)
		    {
		       ++hit;
		       continue;
		    }
		  std::bitset<NSEs> band = (sp->_engine & se_enabled);
		  if (band.count() == 0)
		    {
		       ++hit;
		       continue;
		    }
		  snippets.push_back(sp);
		  ++hit;
	       }

	     std::stable_sort(snippets.begin(),snippets.end(),search_snippet::max_seeks_ir);
	     
	     std::string cluster_str;
	     if (!clusterize)
	       cluster_str = static_renderer::render_cluster_label(clusters[c]);
	     else cluster_str = static_renderer::render_cluster_label_query_link(url_encoded_query,
										 clusters[c],exports);
	     size_t nsps = snippets.size();
	     for (size_t i=0;i<nsps;i++)
	       cluster_str += snippets.at(i)->to_html_with_highlight(words,base_url);
	     cluster_str += "</ol>";
	     
	     std::string cl = rplcnt;
	     if (k>1)
	       cl += miscutil::to_string(l++);
	     miscutil::add_map_entry(exports,cl.c_str(),1,cluster_str.c_str(),1);
	  }
	
	// kill remaining cluster slots.
	for (short c=l;c<=template_K;c++)
	  {
	     std::string hcl = "have-cluster" + miscutil::to_string(c);
	     cgi::map_block_killer(exports,hcl.c_str());
	  }
     }
   
   std::string static_renderer::render_cluster_label(const cluster &cl)
     {
	const char *clabel_encoded = encode::html_encode(cl._label.c_str());
	std::string slabel = "(" + miscutil::to_string(cl._cpoints.size()) + ")";
	const char *slabel_encoded = encode::html_encode(slabel.c_str());
	std::string html_label = "<h2>" + std::string(clabel_encoded) 
	  + " <font size=\"2\">" + std::string(slabel_encoded) + "</font></h2><br><ol>";
	free_const(clabel_encoded);
	free_const(slabel_encoded);
	return html_label;
     }
      
   std::string static_renderer::render_cluster_label_query_link(const std::string &url_encoded_query,
								const cluster &cl,
								const hash_map<const char*,const char*,hash<const char*>,eqstr> *exports,
								const std::string &cgi_base)
     {
	const char *base_url = miscutil::lookup(exports,"base-url");
	std::string base_url_str = "";
	if (base_url)
	  base_url_str = std::string(base_url);
	char *clabel_url_enc = encode::url_encode(cl._label.c_str());
	char *clabel_html_enc = encode::html_encode(cl._label.c_str());
	std::string clabel_url_enc_str = std::string(clabel_url_enc);
	free(clabel_url_enc);
	std::string clabel_html_enc_str = std::string(clabel_html_enc);
	free(clabel_html_enc);
		
	std::string slabel = "(" + miscutil::to_string(cl._cpoints.size()) + ")";
	char *slabel_html_enc = encode::html_encode(slabel.c_str());
	std::string slabel_html_enc_str = std::string(slabel_html_enc);
	free(slabel_html_enc);
		
	std::string label_query = url_encoded_query + "+" + clabel_url_enc_str;
	std::string html_label = "<h2><a class=\"label\" href=\"" + base_url_str + cgi_base + "q=" + label_query
	  + "&amp;page=1&amp;expansion=1&amp;action=expand\">" + clabel_html_enc_str 
	  + "</a><font size=\"2\"> " + slabel_html_enc_str + "</font></h2><br><ol>";
	return html_label;
     }
      
   void static_renderer::render_current_page(const hash_map<const char*, const char*, hash<const char*>, eqstr> *parameters,
					     hash_map<const char*,const char*,hash<const char*>,eqstr> *exports,
					     int &current_page)
     {
	const char *current_page_str = miscutil::lookup(parameters,"page");
	if (!current_page_str)
	  current_page = 0;
	else current_page = atoi(current_page_str);
	if (current_page == 0) current_page = 1;
	std::string cp_str = miscutil::to_string(current_page);
	miscutil::add_map_entry(exports,"$xxpage",1,cp_str.c_str(),1);
     }
   
   void static_renderer::render_expansion(const hash_map<const char*, const char*, hash<const char*>, eqstr> *parameters,
					  hash_map<const char*,const char*,hash<const char*>,eqstr> *exports,
					  std::string &expansion)
     {
	const char *expansion_str = miscutil::lookup(parameters,"expansion");
	miscutil::add_map_entry(exports,"$xxexp",1,expansion_str,1);
	int expn = atoi(expansion_str)+1;
	std::string expn_str = miscutil::to_string(expn);
	miscutil::add_map_entry(exports,"$xxexpn",1,expn_str.c_str(),1);
	expansion = std::string(expansion_str);
     }
   
   void static_renderer::render_next_page_link(const int &current_page,
					       const size_t &snippets_size,
					       const std::string &url_encoded_query,
					       const std::string &expansion,
					       const std::string &engines,
					       const hash_map<const char*,const char*,hash<const char*>,eqstr> *parameters,
					       hash_map<const char*,const char*,hash<const char*>,eqstr> *exports,
					       const std::string &cgi_base)
     {
	int rpp = websearch::_wconfig->_Nr;
	const char *rpp_str = miscutil::lookup(parameters,"rpp");
	if (rpp_str)
	  rpp = atoi(rpp_str);
	double nl = snippets_size / static_cast<double>(rpp);
	     
	if (current_page < nl)
	  {
	     const char *base_url = miscutil::lookup(exports,"base-url");
	     std::string base_url_str;
	     if (base_url)
	       base_url_str = std::string(base_url);
	     std::string rpp_s;
	     if (rpp_str)
	       rpp_s = rpp_str;
	     std::string np_str = miscutil::to_string(current_page+1);
	     std::string np_link = "<a href=\"" + base_url_str + cgi_base + "page=" + np_str + "&amp;q="
	       + url_encoded_query + "&amp;expansion=" + expansion + "&amp;action=page&amp;engines="
	       + engines + "&amp;rpp=" + rpp_s + "\"  id=\"search_page_next\" title=\"Next (ctrl+&gt;)\">&nbsp;</a>";
	     miscutil::add_map_entry(exports,"$xxnext",1,np_link.c_str(),1);
	  }
	else miscutil::add_map_entry(exports,"$xxnext",1,strdup(""),0);
     }
      
   void static_renderer::render_prev_page_link(const int &current_page,
					       const size_t &snippets_size,
					       const std::string &url_encoded_query,
					       const std::string &expansion,
					       const std::string &engines,
					       const hash_map<const char*,const char*,hash<const char*>,eqstr> *parameters,
					       hash_map<const char*,const char*,hash<const char*>,eqstr> *exports,
					       const std::string &cgi_base)
     {
	 if (current_page > 1)
	  {
	     std::string pp_str = miscutil::to_string(current_page-1);
	     const char *base_url = miscutil::lookup(exports,"base-url");
	     std::string base_url_str = "";
	     if (base_url)
	       base_url_str = std::string(base_url);
	     std::string rpp_s;
	     const char *rpp_str = miscutil::lookup(parameters,"rpp");
	     if (rpp_str)
	       rpp_s = rpp_str;
	     std::string pp_link = "<a href=\"" + base_url_str + cgi_base + "page=" + pp_str + "&amp;q="
	       + url_encoded_query + "&amp;expansion=" + expansion + "&amp;action=page&amp;engines=" 
	       + engines + "&amp;rpp=" + rpp_s + "\"  id=\"search_page_prev\" title=\"Previous (ctrl+&lt;)\">&nbsp;</a>";
	     miscutil::add_map_entry(exports,"$xxprev",1,pp_link.c_str(),1);
	  }
	else miscutil::add_map_entry(exports,"$xxprev",1,strdup(""),0);
     }

   void static_renderer::render_nclusters(const hash_map<const char*, const char*, hash<const char*>, eqstr> *parameters,
					  hash_map<const char*,const char*,hash<const char*>,eqstr> *exports)
     {
	if (!websearch::_wconfig->_clustering)
	  {
	     cgi::map_block_killer(exports,"have-clustering");
	     return;
	  }
	else cgi::map_block_killer(exports,"not-have-clustering");
		
	const char *nclusters_str = miscutil::lookup(parameters,"clusters");
	
	if (!nclusters_str)
	  miscutil::add_map_entry(exports,"$xxnclust",1,strdup("10"),0); // default number of clusters is 10.
	else
	  {
	     miscutil::add_map_entry(exports,"$xxclust",1,nclusters_str,1);
	     int nclust = atoi(nclusters_str)+1;
	     std::string nclust_str = miscutil::to_string(nclust);
	     miscutil::add_map_entry(exports,"$xxnclust",1,nclust_str.c_str(),1);
	  }
     }
   
   hash_map<const char*,const char*,hash<const char*>,eqstr>* static_renderer::websearch_exports(client_state *csp,
												 const std::vector<std::pair<std::string,std::string> > *param_exports)
     {
	hash_map<const char*,const char*,hash<const char*>,eqstr> *exports
	  = cgi::default_exports(csp,"");
	
	// we need to inject a remote base location for remote web access.
	// the injected header, if it exists is Seeks-Remote-Location
	std::string base_url = "";
	std::list<const char*>::const_iterator sit = csp->_headers.begin();
	while(sit!=csp->_headers.end())
	  {
	     if (miscutil::strncmpic((*sit),"Seeks-Remote-Location:",22) == 0)
	       {
		  base_url = (*sit);
		  size_t pos = base_url.find_first_of(" ");
		  try
		    {
		       base_url = base_url.substr(pos+1);
		    }
		  catch (std::exception &e)
		    {
		       base_url = "";
		       break;
		    }
		  break;
	       }
	     ++sit;
	  }
	miscutil::add_map_entry(exports,"base-url",1,base_url.c_str(),1);
	
	if (!websearch::_wconfig->_js) // no javascript required
	  {
	     cgi::map_block_killer(exports,"websearch-have-js");
	  }
	
	if (websearch::_wconfig->_content_analysis)
	  miscutil::add_map_entry(exports,"websearch-content-analysis",1,"content_analysis",1);
	else miscutil::add_map_entry(exports,"websearch-content-analysis",1,strdup(""),0);
	
	if (websearch::_wconfig->_thumbs)
	  miscutil::add_map_entry(exports,"websearch-thumbs",1,"thumbs",1);
	else miscutil::add_map_entry(exports,"websearch-thumbs",1,strdup(""),0);
	
	if (websearch::_wconfig->_clustering)
	  miscutil::add_map_entry(exports,"websearch-clustering",1,"clustering",1);
	else miscutil::add_map_entry(exports,"websearch-clustering",1,strdup(""),0);
		
	// image websearch, if available.
#ifndef FEATURE_IMG_WEBSEARCH_PLUGIN
	cgi::map_block_killer(exports,"img-websearch");
#endif
	
	// node IP rendering.
	if (!websearch::_wconfig->_show_node_ip)
	  cgi::map_block_killer(exports,"have-show-node-ip");

	// other parameters.
	if (param_exports)
	  {
	     for (size_t i=0;i<param_exports->size();i++)
	       {
		  miscutil::add_map_entry(exports,param_exports->at(i).first.c_str(),1,
					  param_exports->at(i).second.c_str(),1);
	       }
	  }
		
	return exports;
     }
      
  /*- rendering. -*/
  sp_err static_renderer::render_hp(client_state *csp,http_response *rsp)
     {
	static const char *hp_tmpl_name = "websearch/templates/seeks_ws_hp.html";
	
	hash_map<const char*,const char*,hash<const char*>,eqstr> *exports
	  = static_renderer::websearch_exports(csp);
		
	sp_err err = cgi::template_fill_for_cgi(csp,hp_tmpl_name,
						(seeks_proxy::_datadir.empty() ? plugin_manager::_plugin_repository.c_str()
						 : std::string(seeks_proxy::_datadir + "plugins/").c_str()),
						exports,rsp);
	
	return err;
     }
      
  sp_err static_renderer::render_result_page_static(const std::vector<search_snippet*> &snippets,
						    client_state *csp, http_response *rsp,
						    const hash_map<const char*, const char*, hash<const char*>, eqstr> *parameters,
						    const query_context *qc)
     {
	static const char *result_tmpl_name = "websearch/templates/seeks_result_template.html";
	return static_renderer::render_result_page_static(snippets,csp,rsp,parameters,qc,result_tmpl_name);
     }
   
   sp_err static_renderer::render_result_page_static(const std::vector<search_snippet*> &snippets,
						     client_state *csp, http_response *rsp,
						     const hash_map<const char*, const char*, hash<const char*>, eqstr> *parameters,
						     const query_context *qc,
						     const std::string &result_tmpl_name,
						     const std::string &cgi_base,
						     const std::vector<std::pair<std::string,std::string> > *param_exports)
     {
	hash_map<const char*,const char*,hash<const char*>,eqstr> *exports
	  = static_renderer::websearch_exports(csp,param_exports);

     // query.
     std::string html_encoded_query;
     std::string url_encoded_query;
     static_renderer::render_query(parameters,exports,html_encoded_query,
				   url_encoded_query);
    
     // clean query.
     std::string query_clean;
     static_renderer::render_clean_query(html_encoded_query,
					 exports,query_clean);
     
     // current page.
     int current_page = -1;
     static_renderer::render_current_page(parameters,exports,current_page);
     
     // suggestions.
     static_renderer::render_suggestions(qc,exports,cgi_base);
     
     // language.
     static_renderer::render_lang(qc,exports);

     // engines.
     std::string engines;
     static_renderer::render_engines(parameters,exports,engines);
          
     // TODO: check whether we have some results.
     /* if (snippets->empty())
       {
	  // no results found.
	  std::string no_results = "";
	  return SP_ERR_OK;
       } */
     
     // search snippets.
     static_renderer::render_snippets(query_clean,current_page,snippets,parameters,exports);
     
     // expand button.
     std::string expansion;
     static_renderer::render_expansion(parameters,exports,expansion);
     
     // next link.
     static_renderer::render_next_page_link(current_page,snippets.size(),
					    url_encoded_query,expansion,engines,parameters,
					    exports,cgi_base);
     
     // previous link.
     static_renderer::render_prev_page_link(current_page,snippets.size(),
					    url_encoded_query,expansion,engines,
					    parameters,exports,cgi_base);

     // cluster link.
     static_renderer::render_nclusters(parameters,exports);
     
     // rendering.
     sp_err err = cgi::template_fill_for_cgi(csp,result_tmpl_name.c_str(),
					     (seeks_proxy::_datadir.empty() ? plugin_manager::_plugin_repository.c_str()
					      : std::string(seeks_proxy::_datadir + "plugins/").c_str()),
					     exports,rsp);
     
     return err;
  }

   sp_err static_renderer::render_clustered_result_page_static(cluster *clusters,
							       const short &K,
							       client_state *csp, http_response *rsp,
							       const hash_map<const char*, const char*, hash<const char*>, eqstr> *parameters,
							       const query_context *qc)
     {
	static const char *result_tmpl_name = "websearch/templates/seeks_result_template.html";
	
	hash_map<const char*,const char*,hash<const char*>,eqstr> *exports
	  = static_renderer::websearch_exports(csp);
	
	// query.
	std::string html_encoded_query;
	std::string url_encoded_query;
	static_renderer::render_query(parameters,exports,html_encoded_query,
				      url_encoded_query);
	
	// clean query.
	std::string query_clean;
	static_renderer::render_clean_query(html_encoded_query,
					    exports,query_clean);
	
	// current page.
	int current_page = -1;
	static_renderer::render_current_page(parameters,exports,current_page);
	
	// suggestions.
	static_renderer::render_suggestions(qc,exports);
	
	// language.
	static_renderer::render_lang(qc,exports);
	
	// engines.
	std::string engines;
	static_renderer::render_engines(parameters,exports,engines);
		
	// search snippets.
	static_renderer::render_clustered_snippets(query_clean,url_encoded_query,current_page,
						   clusters,K,qc,parameters,
						   exports);
	
	// expand button.
	std::string expansion;
	static_renderer::render_expansion(parameters,exports,expansion);
	
	// next link.
	/* static_renderer::render_next_page_link(current_page,snippets.size(),
					       html_encoded_query,expansion,exports); */
		
	// previous link.
	/* static_renderer::render_prev_page_link(current_page,snippets.size(),
					       html_encoded_query,expansion,exports); */
     
	// cluster link.
	static_renderer::render_nclusters(parameters,exports);
	
	// rendering.
	sp_err err = cgi::template_fill_for_cgi(csp,result_tmpl_name,
						(seeks_proxy::_datadir.empty() ? plugin_manager::_plugin_repository.c_str()
						 : std::string(seeks_proxy::_datadir + "plugins/").c_str()),
						exports,rsp);
	
	return err;
     }

   // mode: urls = 0, titles = 1.
   sp_err static_renderer::render_neighbors_result_page(client_state *csp, http_response *rsp,
							const hash_map<const char*, const char*, hash<const char*>, eqstr> *parameters,
							query_context *qc, const int &mode)
     {
	if (mode > 1)
	  return SP_ERR_OK; // wrong mode, do nothing.
	
	hash_map<uint32_t,search_snippet*,id_hash_uint> hsnippets;
	hash_map<uint32_t,search_snippet*,id_hash_uint>::iterator hit;
	size_t nsnippets = qc->_cached_snippets.size();
	
	/**
	 * Instanciate an LSH uniform hashtable. Parameters are adhoc, based on experience.
	 */
	LSHSystemHamming *lsh_ham = new LSHSystemHamming(7,30);
	LSHUniformHashTableHamming ulsh_ham(*lsh_ham,nsnippets);
	
	for (size_t i=0;i<nsnippets;i++)
	  {
	     search_snippet *sp = qc->_cached_snippets.at(i);
	     if (mode == 0)
	       {
		  std::string surl = urlmatch::strip_url(sp->_url);
		  ulsh_ham.add(surl,lsh_ham->_Ld);
	       }
	     else if (mode == 1)
	       {
		  std::string lctitle = sp->_title;
		  std::transform(lctitle.begin(),lctitle.end(),lctitle.begin(),tolower);
		  ulsh_ham.add(lctitle,lsh_ham->_Ld);
	       }
	  }
			
	int k=nsnippets;
	for (size_t i=0;i<nsnippets;i++)
	  {
	     search_snippet *sp = qc->_cached_snippets.at(i);
	     if ((hit=hsnippets.find(sp->_id))==hsnippets.end())
	       {
		  sp->_seeks_ir = k--;
		  hsnippets.insert(std::pair<uint32_t,search_snippet*>(sp->_id,sp));
	       }
	     else continue;
		  
	     //std::cerr << "original url: " << sp->_url << std::endl;
	     
	     /**
	      * get similar results. Right now we're not using the probability, but we could
	      * in the future, e.g. to prune some outliers.
	      */
	     std::map<double,const std::string,std::greater<double> > mres;
	     if (mode == 0)
	       {
		  std::string surl = urlmatch::strip_url(sp->_url);
		  mres = ulsh_ham.getLEltsWithProbabilities(surl,lsh_ham->_Ld);
	       }
	     else if (mode == 1)
	       {
		  std::string lctitle = sp->_title;
		  std::transform(lctitle.begin(),lctitle.end(),lctitle.begin(),tolower);
		  mres = ulsh_ham.getLEltsWithProbabilities(lctitle,lsh_ham->_Ld);
	       }
	     std::map<double,const std::string,std::greater<double> >::const_iterator mit = mres.begin();
	     while(mit!=mres.end())
	       {
		  //std::cerr << "lsh url: " << (*mit).second << " -- prob: " << (*mit).first << std::endl;
		  
		  search_snippet *sp2 = NULL;
		  if (mode == 0)
		    sp2 = qc->get_cached_snippet((*mit).second);
		  else if (mode == 1)
		    sp2 = qc->get_cached_snippet_title((*mit).second.c_str());
		  
		  if ((hit=hsnippets.find(sp2->_id))==hsnippets.end())
		    {
		       sp2->_seeks_ir = k--;
		       hsnippets.insert(std::pair<uint32_t,search_snippet*>(sp2->_id,sp2));
		    }
		  ++mit;
	       }
	  }
	std::vector<search_snippet*> snippets;
	snippets.reserve(hsnippets.size());
	hit = hsnippets.begin();
	while(hit!=hsnippets.end())
	  {
	     snippets.push_back((*hit).second);
	     ++hit;
	  }
	std::sort(snippets.begin(),snippets.end(),search_snippet::max_seeks_ir);	
	
	// static rendering.
	return static_renderer::render_result_page_static(snippets,csp,rsp,parameters,qc);
     }

} /* end of namespace. */
