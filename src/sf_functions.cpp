#include <Rcpp.h>
#include <R_ext/Rdynload.h>
#include <R_ext/Riconv.h>

#ifndef PCRE2_CODE_UNIT_WIDTH
#define PCRE2_CODE_UNIT_WIDTH 8
#endif
#ifndef HAVE_CONFIG_H
#define HAVE_CONFIG_H
#endif
#include "PCRE2/pcre2.h"

#include "sf_altrep.h"
#include <fstream> // ifstream, ofstream
using namespace Rcpp;

////////////////////////////////////////////////////////////////////////////////

// [[Rcpp::export(rng = false)]]
std::string get_string_type(SEXP x) {
  rstring_type rx = get_rstring_type_internal(x);
  switch(rx) {
  case rstring_type::NORMAL:
    return "normal vector";
  case rstring_type::SF_VEC:
    return "stringfish vector";
  case rstring_type::SF_VEC_MATERIALIZED:
    return "stringfish vector (materialized)";
  case rstring_type::OTHER_ALT_REP:
    return "other alt-rep vector";
  default:
    throw std::runtime_error("get_string_type error");
  }
}

////////////////////////////////////////////////////////////////////////////////

// [[Rcpp::export(rng = false)]]
SEXP materialize(SEXP x) {
  ALTVEC_DATAPTR(x);
  return x;
}


////////////////////////////////////////////////////////////////////////////////

// [[Rcpp::export(rng = false)]]
SEXP sf_vector(size_t len) {
  auto ret = new sf_vec_data(len);
  return sf_vec::Make(ret, true);
}


////////////////////////////////////////////////////////////////////////////////

// C only export manually - no checking of valid object is done here
sf_vec_data & sf_vec_data_ref(SEXP x) {
  return *reinterpret_cast<sf_vec_data*>(R_ExternalPtrAddr(R_altrep_data1(x)));
}

////////////////////////////////////////////////////////////////////////////////

// [[Rcpp::export(rng = false)]]
void sf_assign(SEXP x, size_t i, SEXP e) {
  if(TYPEOF(e) != STRSXP || Rf_xlength(e) != 1) {
    throw std::runtime_error("e must be a string of length 1");
  }
  if(i == 0) {
    throw std::runtime_error("i must be > 0");
  }
  i--;
  rstring_type rx = get_rstring_type_internal(x);
  switch(rx) {
  case rstring_type::SF_VEC:
  {
    sf_vec_data & ref = sf_vec_data_ref(x);
    ref[i] = sfstring(STRING_ELT(e,0));
  }
    break;
  case rstring_type::NORMAL:
  case rstring_type::SF_VEC_MATERIALIZED:
  case rstring_type::OTHER_ALT_REP:
  default:
    SET_STRING_ELT(x, i, STRING_ELT(e,0));
    break;
  }
}

////////////////////////////////////////////////////////////////////////////////

struct iconv_wrapper {
  void * cd;
  std::string output;
  iconv_wrapper(const char * to, const char * from) {
    cd = Riconv_open(to, from);
  }
  const char * convert(const char * ptr, size_t len) {
    // 4 bytes should always be enough? 
    // UTF-8 = max 4 bytes per code point, but what about other encoding?
    // what about multi-code point characters?
    output.resize(len * 4);
    size_t outlen = output.size();
    char * outptr =  &output[0];
    size_t res = Riconv(cd, &ptr, &len, &outptr, &outlen);
    if(res == (size_t)(-1)) throw std::runtime_error("invalid input sequence");
    //reset state -- not needed?
    // Riconv(cd, nullptr, nullptr, nullptr, nullptr);
    size_t bytes_written = output.size() - outlen;
    output.resize(bytes_written);
    return output.c_str();
  }
  iconv_wrapper(const iconv_wrapper&) = delete;
  ~iconv_wrapper() {
    Riconv_close(cd);
  }
};

// [[Rcpp::export(rng = false)]]
SEXP sf_iconv(SEXP x, std::string from, std::string to) {
  cetype_t encoding;
  if(to == "UTF-8") {
    encoding = CE_UTF8;
  } else if(to == "latin1") {
    encoding = CE_LATIN1;
  } else {
    encoding = CE_NATIVE;
  }
  iconv_wrapper iw(to.c_str(), from.c_str());
  RStringIndexer rsi(x);
  size_t len = rsi.size();
  SEXP ret = PROTECT(sf_vector(len));
  sf_vec_data & ref = sf_vec_data_ref(ret);
  for(size_t i=0; i<len; i++) {
    auto q = rsi.getCharLenCE(i);
    if(q.ptr == nullptr) {
      ref[i] = sfstring(NA_STRING);
    } else {
      ref[i] = sfstring(iw.convert(q.ptr, q.len), encoding);
    }
  }
  UNPROTECT(1);
  return ret;
}

////////////////////////////////////////////////////////////////////////////////

// [[Rcpp::export(rng = false)]]
SEXP convert_to_sf(SEXP x) {
  size_t len = Rf_xlength(x);
  SEXP ret = PROTECT(sf_vector(len));
  sf_vec_data & ref = sf_vec_data_ref(ret);
  for(size_t i=0; i<len; i++) {
    ref[i] = sfstring(STRING_ELT(x,i));
  }
  UNPROTECT(1);
  return ret;
}

////////////////////////////////////////////////////////////////////////////////

// [[Rcpp::export(rng = false)]]
IntegerVector sf_nchar(SEXP x, std::string type = "chars") {
  RStringIndexer rsi(x);
  size_t len = rsi.size();
  IntegerVector ret(len);
  int * optr = INTEGER(ret);
  if(type == "chars") {
    for(size_t i=0; i<len; i++) {
      auto q = rsi.getCharLenCE(i);
      if(q.ptr == nullptr) {
        optr[i] = NA_INTEGER;
      } else if(q.enc == CE_UTF8) {
        optr[i] = code_points(q.ptr);
      } else {
        optr[i] = static_cast<int>( strlen(q.ptr) );
      }
    }
  } else if(type == "bytes") {
    for(size_t i=0; i<len; i++) {
      auto q = rsi.getCharLenCE(i);
      if(q.ptr == nullptr) {
        optr[i] = NA_INTEGER;
      } else {
        optr[i] = static_cast<int>( strlen(q.ptr) );
      }
    }
  } else {
    throw std::runtime_error("type must be chars or bytes");
  }
  return ret;
}

////////////////////////////////////////////////////////////////////////////////

inline sfstring sf_substr_internal(const char * x, const int len, const cetype_t type, int start, int stop) {
  if(x == nullptr) return sfstring(NA_STRING);
  if(len == 0) return sfstring("", CE_NATIVE);
  if(start > len) return sfstring("", CE_NATIVE);
  
  if(type == CE_UTF8) {
    int clen = code_points(x);
    if(start > clen) return sfstring("", CE_NATIVE);
    // for utf-8, stop is one-based
    // we want the end of the last byte for the last character so we overshoot 
    // by one byte to find the boundary
    if(start < 0 || stop < 0) {
      start = start < 0 ? (clen+start) : (start-1);
      stop = stop < 0 ? (clen+stop+1) : (stop); // zero based
    } else {
      start = start-1;
      stop = stop;
    }
    
    if((stop-1) < start) return sfstring("", type);
    if(stop < 1) return sfstring("", type);
    if(start < 0) start = 0;
    
    const char * p = x;
    int start_byte = 0;
    int outlen = 0;
    int count = 0;
    while(count <= start) {
      if(*p == 0) return sfstring("", type);
      count += ((*p & 0xc0) != 0x80); // b11000000, b10000000
      p++;
      start_byte++;
    }
    while(count <= stop) {
      if(*p == 0) {
        outlen++;
        break;
      }
      count += ((*p & 0xc0) != 0x80); // b11000000, b10000000
      p++;
      outlen++;
    }
    return sfstring(std::string(x + start_byte - 1, outlen), type);
  } else {
    start = start < 0 ? (len+start) : (start-1); // zero based
    stop = stop < 0 ? (len+stop) : (stop-1); // zero based
    if(stop < start) return sfstring("", CE_NATIVE);
    if(stop >= len) stop = len - 1;
    if(stop < 0) return sfstring("", CE_NATIVE);
    if(start < 0) start = 0;
    return sfstring(std::string(x + start, stop - start + 1), type);
  }
}

// [[Rcpp::export(rng = false)]]
SEXP sf_substr(SEXP x, IntegerVector start, IntegerVector stop) {
  size_t start_size = Rf_xlength(start);
  size_t stop_size = Rf_xlength(stop);
  
  for(size_t i=0; i<start_size; i++) {
    if(start[i] == NA_INTEGER) throw std::runtime_error("no NA start values allowed");
  }
  
  for(size_t i=0; i<start_size; i++) {
    if(stop[i] == NA_INTEGER) throw std::runtime_error("no NA stop values allowed");
  }
  
  RStringIndexer rsi(x);
  size_t len = rsi.size();
  if(start_size != len && start_size != 1) throw std::runtime_error("length of start must be 1 or the same as x");
  if(stop_size != len && stop_size != 1) throw std::runtime_error("length of stop must be 1 or the same as x");
  SEXP ret = PROTECT(sf_vector(len));
  sf_vec_data & ref = sf_vec_data_ref(ret);
  for(size_t i=0; i<len; i++) {
    auto q = rsi.getCharLenCE(i);
    ref[i] = sf_substr_internal(q.ptr, q.len, q.enc, start[start_size == 1 ? 0 : i], stop[stop_size == 1 ? 0 : i]);
  }
  UNPROTECT(1);
  return ret;
}

////////////////////////////////////////////////////////////////////////////////

// Called by sf_paste -- valid input already determined
// [[Rcpp::export(rng = false)]]
SEXP c_sf_paste(List dots, SEXP sep) {
  size_t maxlen = 1;

  RStringIndexer sr(sep);
  if(sr.size() != 1) throw std::runtime_error("sep should be a character vector of length 1");
  auto sr_element = sr.getCharLenCE(0);
  std::string sep_string(sr_element.ptr, sr_element.len);
  
  size_t dotlen = Rf_xlength(dots);
  std::vector<RStringIndexer> rs;
  std::vector<size_t> lens(dotlen);
  std::vector<RStringIndexer::rstring_info> singles(dotlen);
  for(size_t i=0; i<dotlen; i++) {
    SEXP d = dots[i];
    rs.emplace_back(d); // no copy constructor called, since it s deleted
    lens[i] = rs[i].size();
    if(lens[i] == 1) {
      singles[i] = rs[i].getCharLenCE(0);
    }
    if(maxlen == 1 && lens[i] > 1) {
      maxlen = lens[i];
    }
  }
  
  // i indexes return element
  // j index input vector
  SEXP ret = PROTECT(sf_vector(maxlen));
  sf_vec_data & ref = sf_vec_data_ref(ret);
  for(size_t i=0; i<maxlen; i++) {
    std::string temp;
    cetype_t enc = CE_NATIVE;
    bool is_na = false;
    for(size_t j=0; j<dotlen; j++) {
      RStringIndexer::rstring_info q = lens[j] == 1 ? singles[j] : rs[j].getCharLenCE(i);
      if(q.ptr == nullptr) {
        is_na = true;
        break;
      } else {
        enc = choose_enc(enc, q.enc);
        temp += std::string(q.ptr, q.len);
        if(j < (dotlen-1)) temp += sep_string;
      }
    }
    if(is_na) {
      ref[i] = sfstring(NA_STRING);
    } else {
      ref[i] = sfstring(temp, enc);
    }
  }
  UNPROTECT(1);
  return ret;
}

////////////////////////////////////////////////////////////////////////////////

// [[Rcpp::export(rng = false)]]
SEXP sf_collapse(SEXP x, SEXP collapse) {
  RStringIndexer cr(collapse);
  if(cr.size() != 1) throw std::runtime_error("collapse should be a character vector of length 1");
  auto cr_element = cr.getCharLenCE(0);
  std::string collapse_string(cr_element.ptr, cr_element.len);
  
  RStringIndexer xr(x);
  size_t len = xr.size();
  std::string temp;
  cetype_t enc = cr_element.enc;
  for(size_t i=0; i<len; i++) {
    auto q = xr.getCharLenCE(i);
    if(q.ptr == nullptr) return NA_STRING;
    enc = choose_enc(enc, q.enc);
    temp += std::string(q.ptr, q.len);
    if(i < (len-1)) temp += collapse_string;
  }
  // since the return length is 1, there's no point using ALT-REP
  SEXP ret = PROTECT(Rf_allocVector(STRSXP, 1));
  SET_STRING_ELT(ret, 0, Rf_mkCharLenCE(temp.c_str(), temp.size(), enc));
  UNPROTECT(1);
  return(ret);
}

////////////////////////////////////////////////////////////////////////////////
// [[Rcpp::export(rng = false)]]
SEXP sf_readLines(std::string file, std::string encoding = "UTF-8") {
  SEXP ret = PROTECT(sf_vector(0));
  sf_vec_data & ref = sf_vec_data_ref(ret);
  cetype_t enc;
  if(encoding == "UTF-8") {
    enc = CE_UTF8;
  } else if(encoding == "latin1") {
    enc = CE_LATIN1;
  } else if(encoding == "bytes") {
    enc = CE_BYTES;
  } else {
    enc = CE_NATIVE;
  }
  std::ifstream myFile(R_ExpandFileName(file.c_str()), std::ios::in);
  if(!myFile) {
    throw std::runtime_error("Failed to open " + file + ". Check file path.");
  }

  // possibly better way of handling line endings: https://stackoverflow.com/q/6089231/2723734
  std::string str;
  while(std::getline(myFile, str)) {
    if(str.size() > 0 && str.back() == '\r') {
      str.resize(str.size() - 1);
    }
    ref.push_back(sfstring(str, enc));
  }
  UNPROTECT(1);
  return ret;
}

////////////////////////////////////////////////////////////////////////////////

struct pcre2_match_wrapper {
  pcre2_code * re;
  pcre2_match_data * match_data;
  pcre2_match_wrapper(const char * pattern_ptr, bool utf8) {
    int errorcode;
    PCRE2_SIZE erroroffset;
    re = pcre2_compile((PCRE2_SPTR)pattern_ptr, // pattern
                       PCRE2_ZERO_TERMINATED, // length
                       utf8 ? PCRE2_UTF : 0, // PCRE2_UTF, // options -- 0 or PCRE2_UTF
                       &errorcode, // error reporting
                       &erroroffset, // error reporting
                       NULL // compile context
    );
    if(re == NULL) {
      PCRE2_UCHAR buffer[256];
      pcre2_get_error_message(errorcode, buffer, sizeof(buffer));
      throw std::runtime_error("PCRE2 pattern error: " + std::to_string((int)erroroffset) + std::string((char*)buffer));
    }
    // match_data = pcre2_match_data_create_from_pattern(re, NULL);
    match_data = pcre2_match_data_create(0, NULL);
  }
  pcre2_match_wrapper() : re(nullptr), match_data(nullptr) {}
  pcre2_match_wrapper & operator=(const pcre2_match_wrapper &) = delete; // copy assignment
  pcre2_match_wrapper & operator=(pcre2_match_wrapper && other) { // move assignment
    if(&other == this) return *this;
    if(re != nullptr) pcre2_code_free(re);
    re = other.re;
    match_data = other.match_data;
    other.re = nullptr;
    other.match_data = nullptr;
    return *this;
  }
  pcre2_match_wrapper(const pcre2_match_wrapper&) = delete; // copy constructor
  ~pcre2_match_wrapper() {
    if(re != nullptr) pcre2_code_free(re);
    if(match_data != nullptr) pcre2_match_data_free(match_data);
  }
  int grepl(const char * subject_ptr) {
    int rc = pcre2_match(re, // compiled pattern
                         (PCRE2_SPTR)subject_ptr, // subject
                         PCRE2_ZERO_TERMINATED, // length
                         0, // start offset
                         0, // options
                         match_data,  // match data block
                         NULL // match context
    );
    if(rc == PCRE2_ERROR_NOMATCH) {
      return 0;
    } else if(rc < 0) {
      throw std::runtime_error("error matching string");
    } else {
      return 1;
    }
  }
};

// [[Rcpp::export(rng = false)]]
LogicalVector sf_grepl(SEXP subject, SEXP pattern, std::string encode_mode = "auto") {
  SEXP pattern_element = STRING_ELT(pattern, 0);
  const char * pattern_ptr = CHAR(pattern_element);
  cetype_t pattern_enc = Rf_getCharCE(pattern_element);
  pcre2_match_wrapper p;
  if(encode_mode == "auto") {
    if(pattern_enc == CE_UTF8) {
      p = pcre2_match_wrapper(pattern_ptr,  true);
    } else {
      p = pcre2_match_wrapper(pattern_ptr,  false);
    }
  } else if(encode_mode == "UTF-8") {
    p = pcre2_match_wrapper(pattern_ptr,  true);
  } else if(encode_mode == "latin1") {
    p = pcre2_match_wrapper(pattern_ptr,  false);
  } else if(encode_mode == "native") {
    p = pcre2_match_wrapper(pattern_ptr,  false);
  } else if(encode_mode == "bytes") {
    p = pcre2_match_wrapper(pattern_ptr,  false);
  } else {
    throw std::runtime_error("encode_mode must be auto, byte or UTF-8");
  }
  
  RStringIndexer cr(subject);
  size_t len = cr.size();
  LogicalVector ret(len);
  int * outptr = LOGICAL(ret);
  for(size_t i=0; i<len; i++) {
    auto q = cr.getCharLenCE(i);
    if(q.ptr == nullptr) {
      outptr[i] = NA_LOGICAL;
      continue;
    }
    outptr[i] = p.grepl(q.ptr);
  }
  return ret;
}

////////////////////////////////////////////////////////////////////////////////
struct pcre2_sub_wrapper {
  pcre2_code * re;
  PCRE2_SPTR replacement;
  std::vector<char> output;
  pcre2_sub_wrapper(const char * pattern_ptr, const char * replacement_ptr, bool utf8) : output(std::vector<char>(20)) {
    int errorcode;
    PCRE2_SIZE erroroffset;
    re = pcre2_compile((PCRE2_SPTR)pattern_ptr, // pattern
                       PCRE2_ZERO_TERMINATED, // length
                       utf8 ? PCRE2_UTF : 0, // PCRE2_UTF, // options -- 0 or PCRE2_UTF
                       &errorcode, // error reporting
                       &erroroffset, // error reporting
                       NULL // compile context
    );
    if(re == NULL) {
      PCRE2_UCHAR buffer[256];
      pcre2_get_error_message(errorcode, buffer, sizeof(buffer));
      throw std::runtime_error("PCRE2 pattern error: " + std::to_string((int)erroroffset) + std::string((char*)buffer));
    }
    replacement = (PCRE2_SPTR)replacement_ptr;
  }
  pcre2_sub_wrapper() : re(nullptr) {}
  pcre2_sub_wrapper & operator=(const pcre2_sub_wrapper &) = delete; // copy assignment
  pcre2_sub_wrapper & operator=(pcre2_sub_wrapper && other) { // move assignment
    if(&other == this) return *this;
    re = other.re;
    replacement = other.replacement;
    output = std::move(other.output);
    other.re = nullptr;
    return *this;
  }
  pcre2_sub_wrapper(const pcre2_sub_wrapper&) = delete; // copy constructor
  ~pcre2_sub_wrapper() {
    if(re != nullptr) pcre2_code_free(re);
  }
  const char * gsub(const char * subject_ptr) {
    PCRE2_SIZE output_len = (PCRE2_SIZE)(output.size() - 1);
    int rc = pcre2_substitute(
      re,                       // Points to the compiled pattern
      (PCRE2_SPTR)subject_ptr,  // Points to the subject string
      PCRE2_ZERO_TERMINATED,    // Length of the subject string
      0,                        // Offset in the subject at which to start matching
      PCRE2_SUBSTITUTE_GLOBAL & PCRE2_SUBSTITUTE_OVERFLOW_LENGTH, // Option bits
      NULL,                     // Points to a match data block, or is NULL
      NULL,                     // Points to a match context, or is NULL
      (PCRE2_SPTR)replacement,  // Points to the replacement string
      PCRE2_ZERO_TERMINATED,    // Length of the replacement string
      (PCRE2_UCHAR*)output.data(), //  Points to the output buffer
      &output_len
    );
    if(rc == PCRE2_ERROR_NOMEMORY) {
      output.resize(output_len + 1);
      rc = pcre2_substitute(
        re,                       // Points to the compiled pattern
        (PCRE2_SPTR)subject_ptr,  // Points to the subject string
        PCRE2_ZERO_TERMINATED,    // Length of the subject string
        0,                        // Offset in the subject at which to start matching
        PCRE2_SUBSTITUTE_GLOBAL & PCRE2_SUBSTITUTE_OVERFLOW_LENGTH, // Option bits
        NULL,                     // Points to a match data block, or is NULL
        NULL,                     // Points to a match context, or is NULL
        (PCRE2_SPTR)replacement,  // Points to the replacement string
        PCRE2_ZERO_TERMINATED,    // Length of the replacement string
        (PCRE2_UCHAR*)output.data(), //  Points to the output buffer
        &output_len
      );
    }
    if(rc < 0) {
      throw std::runtime_error("error matching string: check for matching encoding and proper regex syntax");
    }
    return output.data();
  }
};


// [[Rcpp::export(rng = false)]]
SEXP sf_gsub(SEXP subject, SEXP pattern, SEXP replacement, std::string encode_mode = "auto") {
  SEXP pattern_element = STRING_ELT(pattern, 0);
  const char * pattern_ptr = CHAR(pattern_element);
  cetype_t pattern_enc = Rf_getCharCE(pattern_element);
  
  SEXP replacement_element = STRING_ELT(replacement, 0);
  const char * replacement_ptr = CHAR(replacement_element);
  // cetype_t replacement_enc = Rf_getCharCE(replacement_element);
  
  pcre2_sub_wrapper p;
  cetype_t output_enc;
  if(encode_mode == "auto") {
    output_enc = pattern_enc;
    if(pattern_enc == CE_UTF8) {
      p = pcre2_sub_wrapper(pattern_ptr, replacement_ptr,  true);
    } else {
      p = pcre2_sub_wrapper(pattern_ptr, replacement_ptr,  false);
    }
  } else if(encode_mode == "UTF-8") {
    output_enc = CE_UTF8;
    p = pcre2_sub_wrapper(pattern_ptr, replacement_ptr,  true);
  } else if(encode_mode == "latin1") {
    output_enc = CE_LATIN1;
    p = pcre2_sub_wrapper(pattern_ptr, replacement_ptr,  false);
  } else if(encode_mode == "native") {
    output_enc = CE_NATIVE;
    p = pcre2_sub_wrapper(pattern_ptr, replacement_ptr,  false);
  } else if(encode_mode == "bytes") {
    output_enc = CE_BYTES;
    p = pcre2_sub_wrapper(pattern_ptr, replacement_ptr,  false);
  } else {
    throw std::runtime_error("encode_mode must be auto, byte or UTF-8");
  }
  
  RStringIndexer cr(subject);
  size_t len = cr.size();
  SEXP ret = PROTECT(sf_vector(len));
  sf_vec_data & ref = sf_vec_data_ref(ret);
  for(size_t i=0; i<len; i++) {
    auto q = cr.getCharLenCE(i);
    if(q.ptr == nullptr) {
      ref[i] = sfstring(NA_STRING);
      continue;
    }
    ref[i] = sfstring(p.gsub(q.ptr), output_enc);
  }
  UNPROTECT(1);
  return ret;
}

////////////////////////////////////////////////////////////////////////////////
// [[Rcpp::export]] // RNG needed
SEXP random_strings(const int N, const int string_size = 50, 
                       std::string charset = "abcdefghijklmnopqrstuvwxyz", 
                       std::string vector_mode = "stringfish") {
  if(vector_mode == "stringfish") {
    SEXP ret = PROTECT(sf_vector(N));
    sf_vec_data & ref = sf_vec_data_ref(ret);
    std::string str;
    str.resize(string_size);
    for(int i=0; i<N; i++) {
      std::vector<int> r = Rcpp::as< std::vector<int> >(Rcpp::sample(charset.size(), string_size, true, R_NilValue, false));
      for(int j=0; j<string_size; j++) str[j] = charset[r[j]];
      ref[i] = sfstring(str, CE_NATIVE);
    }
    UNPROTECT(1);
    return ret;
  } else if(vector_mode == "normal") {
    CharacterVector ret(N);
    std::string str;
    str.resize(string_size);
    for(int i=0; i<N; i++) {
      std::vector<int> r = Rcpp::as< std::vector<int> >(Rcpp::sample(charset.size(), string_size, true, R_NilValue, false));
      for(int j=0; j<string_size; j++) str[j] = charset[r[j]];
      ret[i] = str;
    }
    return ret;
  } else {
    throw std::runtime_error("String vector_mode must be 'normal' or 'stringfish'");
  }
}
  
// to do:
// sf_sprintf
// sf_assign
// sf_subset
// sf_strsplit
// sf_reverse
// sf_encoding
// sf_set_encoding


////////////////////////////////////////////////////////////////////////////////

// [[Rcpp::init]]
void sf_export_functions(DllInfo* dll) {
  R_RegisterCCallable("stringfish", "get_string_type", (DL_FUNC) &get_string_type);
  R_RegisterCCallable("stringfish", "materialize", (DL_FUNC) &materialize);
  R_RegisterCCallable("stringfish", "sf_vector", (DL_FUNC) &sf_vector);
  R_RegisterCCallable("stringfish", "sf_vec_data_ref", (DL_FUNC) &sf_vec_data_ref);
  R_RegisterCCallable("stringfish", "sf_assign", (DL_FUNC) &sf_assign);
  R_RegisterCCallable("stringfish", "sf_iconv", (DL_FUNC) &sf_iconv);
  R_RegisterCCallable("stringfish", "convert_to_sf", (DL_FUNC) &convert_to_sf);
  R_RegisterCCallable("stringfish", "sf_nchar", (DL_FUNC) &sf_nchar);
  // R_RegisterCCallable("stringfish", "sfstring sf_substr_internal", (DL_FUNC) &sfstring sf_substr_internal);
  R_RegisterCCallable("stringfish", "sf_substr", (DL_FUNC) &sf_substr);
  R_RegisterCCallable("stringfish", "c_sf_paste", (DL_FUNC) &c_sf_paste);
  R_RegisterCCallable("stringfish", "sf_collapse", (DL_FUNC) &sf_collapse);
  R_RegisterCCallable("stringfish", "sf_readLines", (DL_FUNC) &sf_readLines);
  R_RegisterCCallable("stringfish", "sf_grepl", (DL_FUNC) &sf_grepl);
  R_RegisterCCallable("stringfish", "sf_gsub", (DL_FUNC) &sf_gsub);
  R_RegisterCCallable("stringfish", "random_strings", (DL_FUNC) &random_strings);
}

// END OF SF_FUNCTIONS.CPP

