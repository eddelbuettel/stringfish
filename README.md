stringfish
================

<!-- <img src="hex.png" width = "130" height = "150" align="right" style="border:0px;padding:15px"> -->

[![Build
Status](https://travis-ci.org/traversc/stringfish.svg)](https://travis-ci.org/traversc/stringfish)

*Work in progress*

`stringfish` is a fast framework for performing string and sequence
operations. At a basic level, it uses the `alt-rep` system to speed up
the comptuation of common string operations.

However, the goal of the package is more ambitious: to unify `alt-rep`
string implementations under a common framework.

To understand `stringfish`, some explanation of the `alt-rep` system is
required. The `alt-rep` system (new as of R 3.5.0) allows package
developers to represent R objects using their own custom memory layout,
completely invisible to the user. `stringfish` represents string data as
a simple C++/STL vector, which is very fast lightweight.

However, using normal R functions to process string data (e.g. `substr`,
`gsub`, `paste`, etc.) causes “materialization” of `alt-rep` vectors to
normal R data, which can be a slow process. Therefore, a new framework
that can recognize `alt-rep` vectors and process them without
materialization is needed. That is the purpose `stringfish` attempts to
fill.

## Example

Below is an example that imitates common bioinformatic processing.
First, we read in a bunch of sequencing data (in this case, 1 million
randomly generated amino acid sequences) and then uses `sf_substr` to
chop off two amino acids at the beginning and end of each sequence. The
last operation then uses `sf_grepl` to count the number of “RS” motifs,
a common T-cell Receptor *Influenza* binding motif.

``` r
library(stringfish)
x <- sf_random_strings(1e6, string_size = 20, charset = "ARNDCQEGHILKMFPSTWYV")
x1 <- sf_substr(x, 3, -3)
s1 <- sum(sf_grepl(x, "RS", encode_mode = "byte"))
```

Doing the same thing in base R for
comparison:

``` r
x <- sf_random_strings(1e6, string_size = 20, charset = "ARNDCQEGHILKMFPSTWYV", mode = "normal")
x0 <- substr(x, 3, 18)
s0 <- sum(grepl("RS", x))
```

## Benchmark

A quick benchmark comparing the above example done with `stringfish` or
base R.

![](vignettes/bench_v1.png "bench_v1")

## Installation:

``` r
remotes::install_github("traversc/stringfish")
```

## Currently implemented functions

A list of implemented `stringfish` function and analogous base R
function:

  - `sf_iconv` (`iconv`)
  - `sf_nchar` (`nchar`)
  - `sf_substr` (`substr`)
  - `sf_paste` (`paste0`)
  - `sf_collapse` (`paste0`)
  - `sf_readLines` (`readLines`)
  - `sf_grepl` (`grepl`)
  - `sf_gsub` (`gsub`)

Utility functions:

  - `convert_to_sf` – converts a character vector to a `stringfish`
    vector
  - `get_string_type` – determines string type (whether `alt-rep` or
    normal)
  - `materialize` – converts any `alt-rep` object into a normal R object
  - `inspect` – performs inspection on an object
    (`.Internal(inspect(x))`)
  - `new_sf_vec` – creates a new and empty `stringfish` vector
  - `sf_random_strings` – creates a random strings as either a
    `stringfish` or normal R vector

`stringfish` functions are not intended to exactly replicate their base
R analogues. One systematic difference is `stringfish` does minimal
encoding checks and no re-encoding. Therefore, to combine `latin1` and
`UTF-8` encoded strings, first use `sf_iconv`. Another difference is
that `subject` parameters are always the first argument, to be easier to
use in pipes (`%>%`). E.g., `gsub(pattern, replacement, subject)`
becomes `gsub(subject ,pattern, replacement)`.

## Extensibility

`stringfish` as a framework is intended to be easily extensible. (To do:
fill in this section later)

``` c
// [[Rcpp::depends(stringfish)]]
// [[Rcpp::plugins(cpp11)]]
#include <Rcpp.h>
#include <sf_external.h>
using namespace Rcpp;

// [[Rcpp::export]]
void example(SEXP x) {
  // ...
}
```
