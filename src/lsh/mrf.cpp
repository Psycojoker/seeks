/**
 * The Locality Sensitive Hashing (LSH) library is part of the SEEKS project and
 * does provide several locality sensitive hashing schemes for pattern matching over
 * continuous and discrete spaces.
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

#include "mrf.h"

#include <algorithm>
#include <iostream>

//#define DEBUG

namespace lsh
{

  /*-- str_chain --*/
  str_chain::str_chain(const std::string &str,
		       const int &radius)
    :_radius(radius),_skip(false)
  {
    add_token(str);
    if (str == "<skip>")
      _skip = true;
  }

  str_chain::str_chain(const str_chain &sc)
    :_chain(sc.get_chain()),_radius(sc.get_radius()),
     _skip(sc.has_skip())
  {
  }
  
  void str_chain::add_token(const std::string &str)
  {
    _chain.push_back(str);
  }

  str_chain str_chain::rank_alpha() const
  {
    str_chain cchain = *this;
    
#ifdef DEBUG
    /* std::cout << "cchain: ";
     cchain.print(std::cout); */
#endif

    std::sort(cchain.get_chain_noconst().begin(),cchain.get_chain_noconst().end());
    
#ifdef DEBUG
     /* std::cout << "sorted chain: ";
      cchain.print(std::cout); */
#endif

    return cchain;
  }

  std::ostream& str_chain::print(std::ostream &output) const
  {
    for (size_t i=0;i<_chain.size();i++)
      output << _chain.at(i) << " ";
    output << std::endl;
    return output;
  }

  /*-- mrf --*/

  std::string mrf::_default_delims = " ";
  uint32_t mrf::_skip_token = 0xDEADBEEF;
  uint32_t mrf::_window_length_default = 8;
  uint32_t mrf::_window_length = 8;
  uint32_t mrf::_hctable[] = { 1, 3, 5, 11, 23, 47, 97, 197, 397, 797 };
  double mrf::_epsilon = 1e-6;  // infinitesimal. 

  void mrf::tokenize(const std::string &str,
		     std::vector<std::string> &tokens,
		     const std::string &delim)
  {
    // Skip delimiters at beginning.
    std::string::size_type lastPos = str.find_first_not_of(delim, 0);
    // Find first "non-delimiter".
    std::string::size_type pos = str.find_first_of(delim, lastPos);

    while (std::string::npos != pos || std::string::npos != lastPos)
      {
        // Found a token, add it to the vector.
        tokens.push_back(str.substr(lastPos, pos - lastPos));
        // Skip delimiters.  Note the "not_of"
        lastPos = str.find_first_not_of(delim, pos);
        // Find next "non-delimiter"
        pos = str.find_first_of(delim, lastPos);
      }
  }

   uint32_t mrf::mrf_single_feature(const std::string &str)
     {
	std::vector<std::string> tokens;
	mrf::tokenize(str,tokens,mrf::_default_delims);
	return mrf::mrf_hash(tokens);
     }
      
  void mrf::mrf_features(const std::string &str,
			 std::vector<uint32_t> &features,
			 const int &min_radius,
			 const int &max_radius)
  {
	std::vector<std::string> tokens;
	mrf::tokenize(str,tokens,mrf::_default_delims);
	
	int gen_radius = 0;
	while(!tokens.empty())
	  {
	    mrf::mrf_build(tokens,features,
			   min_radius,max_radius,
			   gen_radius);
	    tokens.erase(tokens.begin());
	    ++gen_radius;
	  }
	std::sort(features.begin(),features.end());
  }

  void mrf::mrf_build(const std::vector<std::string> &tokens,
		      std::vector<uint32_t> &features,
		      const int &min_radius,
		      const int &max_radius,
		      const int &gen_radius)
  {
    // scale the field's window size.
    if (tokens.size() < mrf::_window_length_default)
      mrf::_window_length = tokens.size();

    int tok = 0;
    std::queue<str_chain> chains;
    mrf::mrf_build(tokens,tok,chains,features,
		   min_radius,max_radius,gen_radius);
  
    // reset the field's window size.
    mrf::_window_length = mrf::_window_length_default;
  }

  void mrf::mrf_build(const std::vector<std::string> &tokens,
		      int &tok,
		      std::queue<str_chain> &chains,
		      std::vector<uint32_t> &features,
		      const int &min_radius, const int &max_radius,
		      const int &gen_radius)
  {
    if (chains.empty())
      {
	 int radius_chain = gen_radius+tokens.size()-1;
	 str_chain chain(tokens.at(tok),radius_chain);
	
	if (radius_chain >= min_radius
	    && radius_chain <= max_radius)
	  {
	    //hash chain and add it to features set.
	    uint32_t h = mrf::mrf_hash(chain);
	    features.push_back(h);
	    
#ifdef DEBUG
	    //debug
	    std::cout << tokens.at(tok) << std::endl;
	    std::cout << std::hex << h << std::endl;
	    std::cout << std::endl;
	    //debug
#endif
	  }

	chains.push(chain);

	mrf::mrf_build(tokens,tok,chains,features,
		       min_radius,max_radius,gen_radius);
      }
    else 
      {
	++tok;
	std::queue<str_chain> nchains;
	
	while(!chains.empty())
	  {
	    str_chain chain = chains.front();
	    chains.pop();
	
	    if (chain.size() < mrf::_window_length)
	      {
		// first generated chain: add a token.
		str_chain chain1(chain);
		chain1.add_token(tokens.at(tok));
		chain1.decr_radius();
		
		if (chain1.get_radius() >= min_radius
		    && chain1.get_radius() <= max_radius)
		  {
		    // hash it and add it to features.
		    uint32_t h = mrf::mrf_hash(chain1);
		    features.push_back(h);

#ifdef DEBUG
		    //debug
		    chain1.print(std::cout);
		    std::cout << std::hex << h << std::endl;
		    std::cout << std::endl;
		    //debug
#endif
		  }
		
		// second generated chain: add a 'skip' token.
		str_chain chain2 = chain;
		chain2.add_token("<skip>");
		chain2.set_skip();
		
		nchains.push(chain1);
		nchains.push(chain2);
	      }
	  }

	if (!nchains.empty())
	  mrf::mrf_build(tokens,tok,nchains,features,
			 min_radius,max_radius,gen_radius);
      }
  }

  uint32_t mrf::mrf_hash(const str_chain &chain)
  {
    // rank chains which do not contain any skipped token (i.e. no 
    // order preservation).
    str_chain cchain(chain);
    if (!chain.has_skip())
      cchain = chain.rank_alpha();

    uint32_t h = 0;
    size_t csize = std::min(10,(int)cchain.size());  // beware: hctable may be too small...
    for (size_t i=0;i<csize;i++)
      {
	std::string token = cchain.at(i);
	uint32_t hashed_token = mrf::_skip_token;
	if (token != "<skip>")
	  hashed_token= mrf::SuperFastHash(token.c_str(),token.size());

#ifdef DEBUG
	//debug
	 //std::cout << "hashed token: " << hashed_token << std::endl;
	//debug
#endif

	h += hashed_token * mrf::_hctable[i]; // beware...
      }
    return h;
  }

   uint32_t mrf::mrf_hash(const std::vector<std::string> &tokens)
     {
	uint32_t h = 0;
	size_t csize = std::min(10,(int)tokens.size());
	for (size_t i=0;i<csize;i++)
	  {
	     std::string token = tokens.at(i);
	     uint32_t hashed_token= mrf::SuperFastHash(token.c_str(),token.size());
	     
#ifdef DEBUG
	     //debug
	     //std::cout << "hashed token: " << hashed_token << std::endl;
	     //debug
#endif
	     
	     h += hashed_token * mrf::_hctable[i]; // beware...
	  }
	return h;
     }
   
  // Paul Hsieh's super fast hash function.

#include "stdint.h"
#undef get16bits
#if (defined(__GNUC__) && defined(__i386__)) || defined(__WATCOMC__) \
  || defined(_MSC_VER) || defined (__BORLANDC__) || defined (__TURBOC__)
#define get16bits(d) (*((const uint16_t *) (d)))
#endif

#if !defined (get16bits)
#define get16bits(d) ((((uint32_t)(((const uint8_t *)(d))[1])) << 8)\
		      +(uint32_t)(((const uint8_t *)(d))[0]) )
#endif


  uint32_t mrf::SuperFastHash (const char * data, uint32_t len) {
    uint32_t hash = len, tmp;
    int rem;

    if (len <= 0 || data == NULL) return 0;

    rem = len & 3;
    len >>= 2;

    /* Main loop */
    for (;len > 0; len--) {
      hash  += get16bits (data);
      tmp    = (get16bits (data+2) << 11) ^ hash;
      hash   = (hash << 16) ^ tmp;
      data  += 2*sizeof (uint16_t);
      hash  += hash >> 11;
    }

    /* Handle end cases */
    switch (rem) {
    case 3: hash += get16bits (data);
      hash ^= hash << 16;
      hash ^= data[sizeof (uint16_t)] << 18;
      hash += hash >> 11;
      break;
    case 2: hash += get16bits (data);
      hash ^= hash << 11;
      hash += hash >> 17;
      break;
    case 1: hash += *data;
      hash ^= hash << 10;
      hash += hash >> 1;
    }

    /* Force "avalanching" of final 127 bits */
    hash ^= hash << 3;
    hash += hash >> 5;
    hash ^= hash << 4;
    hash += hash >> 17;
    hash ^= hash << 25;
    hash += hash >> 6;

    return hash;
  }

  
  int mrf::hash_compare(const uint32_t &a, const uint32_t &b)
  {
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
  }
  
  // for convenience only.
  double mrf::radiance(const std::string &query1,
		       const std::string &query2)
  {
    return mrf::radiance(query1,query2,0,mrf::_window_length_default);
  }
  
  double mrf::radiance(const std::string &query1,
		       const std::string &query2,
		       const int &min_radius,
		       const int &max_radius)
  {
    // mrf call.
    std::vector<uint32_t> features1;
    mrf::mrf_features(query1,features1,min_radius,max_radius);
    
    std::vector<uint32_t> features2;
    mrf::mrf_features(query2,features2,min_radius,max_radius);
      
    return mrf::radiance(features1,features2);
  }

  double mrf::radiance(const std::vector<uint32_t> &sorted_features1,
		       const std::vector<uint32_t> &sorted_features2)
  {
    // distance computation.
    int common_features = 0;
    int nsf1 = sorted_features1.size();
    int nsf2 = sorted_features2.size();
    int cfeat1 = 0;  // feature counter.
    int cfeat2 = 0;
    while(cfeat1<nsf1)
      {
	int cmp = mrf::hash_compare(sorted_features1.at(cfeat1),
				    sorted_features2.at(cfeat2));
  
	if (cmp > 0)
	  {
	    ++cfeat2; // keep on moving in set 2.
	    if (cfeat2 >= nsf2)
	      break;
	  }
	else if (cmp < 0)
	  {
	    ++cfeat1;
	  }
	else
	  {
	    common_features++;
	    
	    ++cfeat1;
	    ++cfeat2;
	    if (cfeat2 >= nsf2)
	      break;
	  }
      }
    
    double found_only_in_set1 = nsf1 - common_features;
    double found_only_in_set2 = nsf2 - common_features;

    double distance = found_only_in_set1 + found_only_in_set2;

#ifdef DEBUG
    //debug
    std::cout << "nsf1: " << nsf1 << " -- nsf2: " << nsf2 << std::endl;
    std::cout << "common features: " << common_features << std::endl;
    std::cout << "found only in set1: " << found_only_in_set1 << std::endl;
    std::cout << "found only in set2: " << found_only_in_set2 << std::endl;
    //debug
#endif

    // radiance.
    double radiance = (common_features * common_features) / (distance + mrf::_epsilon);

    return radiance;
  }

} /* end of namespace. */