(c++-project
 '(
   "src"
   "elle/elle/src"
   "elle/reactor/src"
   "elle/cryptography/sources"
   )
 "_build/docker/xenial"
 "./drake -j 3 //build"
 ""
 '()
)
