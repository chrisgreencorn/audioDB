#include "lshlib.h"

//#define __LSH_DUMP_CORE_TABLES__
//#define USE_U_FUNCTIONS
#define LSH_BLOCK_FULL_ROWS

void err(char*s){cout << s << endl;exit(2);}

Uns32T get_page_logn(){
  int pagesz = (int)sysconf(_SC_PAGESIZE);
  return (Uns32T)log2((double)pagesz);  
}

unsigned align_up(unsigned x, unsigned w){ return ((x) + ((1<<w)-1) & ~((1<<w)-1)); }

void H::error(const char* a, const char* b, const char *sysFunc) {
  cerr << a << ": " << b << endl;
  if (sysFunc) {
    perror(sysFunc);
  }
  exit(1);
}

H::H(){
  // Delay initialization of lsh functions until we know the parameters
}

H::H(Uns32T kk, Uns32T mm, Uns32T dd, Uns32T NN, Uns32T CC, float ww, float rr):
#ifdef USE_U_FUNCTIONS
  use_u_functions(true),
#else
  use_u_functions(false),
#endif
  maxp(0),
  bucketCount(0),
  pointCount(0),
  N(NN),
  C(CC),
  k(kk),
  m(mm),
  L((mm*(mm-1))/2),
  d(dd),
  w(ww),
  radius(rr)
{

  if(m<2){
    m=2;
    L=1; // check value of L
    cout << "warning: setting m=2, L=1" << endl;
  }
  if(use_u_functions && k%2){
    k++; // make sure k is even
    cout << "warning: setting k even" << endl;
  }
  
  // We have the necessary parameters, so construct hashfunction datastructures
  initialize_lsh_functions();
}

void H::initialize_lsh_functions(){
  H::P = UH_PRIME_DEFAULT;
  
  /* FIXME: don't use time(); instead use /dev/random or similar */
  /* FIXME: write out the seed somewhere, so that we can get
     repeatability */
#ifdef MT19937
  init_genrand(time(NULL));
#else
  srand(time(NULL)); // seed random number generator
#endif
  Uns32T i,j, kk;
#ifdef USE_U_FUNCTIONS
  H::A = new float**[ H::m ]; // m x k x d random projectors
  H::b = new float*[ H::m ];  // m x k random biases
#else
  H::A = new float**[ H::L ]; // m x k x d random projectors
  H::b = new float*[ H::L ];  // m x k random biases
#endif
  H::g = new Uns32T*[ H::L ];   //  L x k random projections 
  assert( H::g && H::A && H::b ); // failure
#ifdef USE_U_FUNCTIONS
  // Use m \times u_i functions \in R^{(k/2) \times (d)}
  // Combine to make L=m(m-1)/2 hash functions \in R^{k \times d}
  for( j = 0; j < H::m ; j++ ){ // m functions u_i(v)
    H::A[j] = new float*[ H::k/2 ];  // k/2 x d  2-stable distribution coefficients     
    H::b[j] = new float[ H::k/2 ];   // bias
    assert( H::A[j] && H::b[j] ); // failure
    for( kk = 0; kk < H::k/2 ; kk++ ){
      H::A[j][kk] = new float[ H::d ];
      assert( H::A[j][kk] ); // failure
      for(Uns32T i = 0 ; i < H::d ; i++ )
	H::A[j][kk][i] = H::randn(); // Normal
      H::b[j][kk] = H::ranf()*H::w;        // Uniform
    }
  }
#else
  // Use m \times u_i functions \in R^{k \times (d)}
  // Combine to make L=m(m-1)/2 hash functions \in R^{k \times d}
  for( j = 0; j < H::L ; j++ ){ // m functions u_i(v)
    H::A[j] = new float*[ H::k ];  // k x d  2-stable distribution coefficients     
    H::b[j] = new float[ H::k ];   // bias
    assert( H::A[j] && H::b[j] ); // failure
    for( kk = 0; kk < H::k ; kk++ ){
      H::A[j][kk] = new float[ H::d ];
      assert( H::A[j][kk] ); // failure
      for(Uns32T i = 0 ; i < H::d ; i++ )
	H::A[j][kk][i] = H::randn(); // Normal
      H::b[j][kk] = H::ranf()*H::w;        // Uniform
    }
  }
#endif

  // Storage for LSH hash function output (Uns32T)
  for( j = 0 ; j < H::L ; j++ ){    // L functions g_j(u_a, u_b) a,b \in nchoosek(m,2)
    H::g[j] = new Uns32T[ H::k ];   // k x 32-bit hash values, gj(v)=[x0 x1 ... xk-1] xk \in Z
    assert( H::g[j] );
  }

  // LSH Hash tables
  H::h = new bucket**[ H::L ];  
  assert( H::h );
  for( j = 0 ; j < H::L ; j++ ){
    H::h[j] = new bucket*[ H::N ];
    assert( H::h[j] );
    for( i = 0 ; i < H::N ; i++)
      H::h[j][i] = 0;
  }

  // Standard hash functions
  H::r1 = new Uns32T*[ H::L ];
  H::r2 = new Uns32T*[ H::L ];
  assert( H::r1 && H::r2 ); // failure
  for( j = 0 ; j < H::L ; j++ ){
    H::r1[ j ] = new Uns32T[ H::k ];
    H::r2[ j ] = new Uns32T[ H::k ];
    assert( H::r1[j] && H::r2[j] ); // failure
    for( i = 0; i<H::k; i++){
      H::r1[j][i] = randr();
      H::r2[j][i] = randr();
    }
  }

  // Storage for whole or partial function evaluation depdenting on USE_U_FUNCTIONS
  H::initialize_partial_functions();
}

void H::initialize_partial_functions(){

#ifdef USE_U_FUNCTIONS
  H::uu = vector<vector<Uns32T> >(H::m);
  for( Uns32T aa=0 ; aa < H::m ; aa++ )
    H::uu[aa] = vector<Uns32T>( H::k/2 );
#else
  H::uu = vector<vector<Uns32T> >(H::L);
  for( Uns32T aa=0 ; aa < H::L ; aa++ )
    H::uu[aa] = vector<Uns32T>( H::k );
#endif
}


// Generate z ~ N(0,1)
float H::randn(){
// Box-Muller
  float x1, x2;
  do{
    x1 = ranf();
  } while (x1 == 0); // cannot take log of 0
  x2 = ranf();
  float z;
  z = sqrtf(-2.0 * logf(x1)) * cosf(2.0 * M_PI * x2);
  return z;
}

float H::ranf(){
#ifdef MT19937
  return (float) genrand_real2();
#else
  return (float)( (double)rand() / ((double)(RAND_MAX)+(double)(1)) );
#endif
}

// range is 1..2^29
/* FIXME: that looks like an ... odd range.  Still. */
Uns32T H::randr(){
#ifdef MT19937
  return (Uns32T)((genrand_int32() >> 3) + 1);
#else
  return (Uns32T) ((rand() >> 2) + 1); 
#endif
}

// Destruct hash tables
H::~H(){
  Uns32T i,j,kk;
#ifdef USE_U_FUNCTIONS
  for( j = 0 ; j < H::m ; j++ ){
    for( kk = 0 ; kk < H::k/2 ; kk++ )
      delete[] A[j][kk];
    delete[] A[j];
  }
  delete[] A;    
  for( j = 0 ; j < H::m ; j++ )
    delete[] b[j];
  delete[] b;
#else
  for( j = 0 ; j < H::L ; j++ ){
    for( kk = 0 ; kk < H::k ; kk++ )
      delete[] A[j][kk];
    delete[] A[j];
  }
  delete[] A;    
  for( j = 0 ; j < H::L ; j++ )
    delete[] b[j];
  delete[] b;
#endif

  for( j = 0 ; j < H::L ; j++ )
    delete[] g[j];
  delete[] g;
  for( j=0 ; j < H::L ; j++ ){
      delete[] H::r1[ j ];
      delete[] H::r2[ j ];
      for(i = 0; i< H::N ; i++)
	delete H::h[ j ][ i ];
      delete[] H::h[ j ];
  }
  delete[] H::r1;
  delete[] H::r2;
  delete[] H::h;
}


// Compute all hash functions for vector v
// #ifdef USE_U_FUNCTIONS use Combination of m \times h_i \in R^{(k/2) \times d}
// to make L \times g_j functions \in Z^k
void H::compute_hash_functions(vector<float>& v){ // v \in R^d
  float iw = 1. / H::w; // hash bucket width
  Uns32T aa, kk;
  if( v.size() != H::d ) 
    error("v.size != H::d","","compute_hash_functions"); // check input vector dimensionality
  double tmp = 0;
  float *pA, *pb;
  Uns32T *pg;
  int dd;
  vector<float>::iterator vi;
  vector<Uns32T>::iterator ui;

#ifdef USE_U_FUNCTIONS
  Uns32T bb;
  // Store m dot products to expand
  for( aa=0; aa < H::m ; aa++ ){
    ui = H::uu[aa].begin();
    for( kk = 0 ; kk < H::k/2 ; kk++ ){
      pb = *( H::b + aa ) + kk;
      pA = * ( * ( H::A + aa ) + kk );
      dd = H::d;
      tmp = 0.;
      vi = v.begin();
      while( dd-- )
	tmp += *pA++ * *vi++;  // project
      tmp += *pb;              // translate
      tmp *= iw;               // scale      
      *ui++ = (Uns32T) floor(tmp);   // floor
    }
  }
  // Binomial combinations of functions u_{a,b} \in Z^{(k/2) \times d}
  Uns32T j;
  for( aa=0, j=0 ; aa < H::m-1 ; aa++ )
    for( bb = aa + 1 ; bb < H::m ; bb++, j++ ){
      pg= *( H::g + j ); // L \times functions g_j(v) \in Z^k      
      // u_1 \in Z^{(k/2) \times d}
      ui = H::uu[aa].begin();
      kk=H::k/2;
      while( kk-- )
	*pg++ = *ui++;      // hash function g_j(v)=[x1 x2 ... x(k/2)]; xk \in Z
      // u_2 \in Z^{(k/2) \times d}
      ui = H::uu[bb].begin();
      kk=H::k/2;
      while( kk--)
	*pg++ = *ui++;      // hash function g_j(v)=[x(k/2+1) x(k/2+2) ... xk]; xk \in Z
    }
#else
  for( aa=0; aa < H::L ; aa++ ){
    ui = H::uu[aa].begin();
    for( kk = 0 ; kk < H::k ; kk++ ){
      pb = *( H::b + aa ) + kk;
      pA = * ( * ( H::A + aa ) + kk );
      dd = H::d;
      tmp = 0.;
      vi = v.begin();
      while( dd-- )
	tmp += *pA++ * *vi++;  // project
      tmp += *pb;              // translate
      tmp *= iw;               // scale      
      *ui++ = (Uns32T) (floor(tmp));   // floor
    }
  }
  // Compute hash functions
  for( aa=0 ; aa < H::L ; aa++ ){
    pg= *( H::g + aa ); // L \times functions g_j(v) \in Z^k      
    // u_1 \in Z^{k \times d}
    ui = H::uu[aa].begin();
    kk=H::k;    
    while( kk-- )
      *pg++ = *ui++;      // hash function g_j(v)=[x1 x2 ... xk]; xk \in Z
  }
#endif
}

// make hash value \in Z
void H::generate_hash_keys(Uns32T*g, Uns32T* r1, Uns32T* r2){
  H::t1 = computeProductModDefaultPrime( g, r1, H::k ) % H::N;
  H::t2 = computeProductModDefaultPrime( g, r2, H::k );
}

#define CR_ASSERT(b){if(!(b)){fprintf(stderr, "ASSERT failed on line %d, file %s.\n", __LINE__, __FILE__); exit(1);}}

// Computes (a.b) mod UH_PRIME_DEFAULT
inline Uns32T H::computeProductModDefaultPrime(Uns32T *a, Uns32T *b, IntT size){
  LongUns64T h = 0;

  for(IntT i = 0; i < size; i++){
    h = h + (LongUns64T)a[i] * (LongUns64T)b[i];
    h = (h & TWO_TO_32_MINUS_1) + 5 * (h >> 32);
    if (h >= UH_PRIME_DEFAULT) {
      h = h - UH_PRIME_DEFAULT;
    }
    CR_ASSERT(h < UH_PRIME_DEFAULT);
  }
  return h;
}

Uns32T H::bucket_insert_point(bucket **pp){
  collisionCount = 0;
  if(!*pp){
    *pp = new bucket();
#ifdef LSH_BLOCK_FULL_ROWS
    (*pp)->t2 = 0; // Use t2 as a collision counter for the row
    (*pp)->next = new bucket();
#endif
  }
#ifdef LSH_BLOCK_FULL_ROWS
  collisionCount = (*pp)->t2;
  if(collisionCount < H::C){ // Block if row is full
    (*pp)->t2++; // Increment collision counter
    pointCount++;
    collisionCount++;
    __bucket_insert_point((*pp)->next); // First bucket holds collision count
  }
#else
  pointCount++;
  __bucket_insert_point(*pp); // No collision count storage
#endif
  return collisionCount;
}

void H::__bucket_insert_point(bucket* p){
  if(p->t2 == IFLAG){ // initialization flag, is it in the domain of t2?
    p->t2 = H::t2;
    bucketCount++; // Record start of new point-locale collision chain
    p->snext = new sbucket();
    __sbucket_insert_point(p->snext);
    return;
  }

  if(p->t2 == H::t2){
    __sbucket_insert_point(p->snext);
    return;
  }

  if(p->next){
    __bucket_insert_point(p->next);
  }

  else{
    p->next = new bucket();
    __bucket_insert_point(p->next);
  }

}

void H::__sbucket_insert_point(sbucket* p){
  if(p->pointID==IFLAG){
    p->pointID = H::p;
    return;
  }

  // Search for pointID
  if(p->snext){
    __sbucket_insert_point(p->snext);
  }
  else{
  // Make new point collision bucket at end of list
  p->snext = new sbucket();
  __sbucket_insert_point(p->snext);
  }
}

inline bucket** H::get_bucket(int j){
  return  *(h+j);
}

// Interface to Locality Sensitive Hashing G
G::G(float ww, Uns32T kk,Uns32T mm, Uns32T dd, Uns32T NN, Uns32T CC, float rr):
  H(kk,mm,dd,NN,CC,ww,rr), // constructor to initialize data structures
  indexName(0),
  lshHeader(0),
  calling_instance(0),
  add_point_callback(0)
{

}

// Serialize from file LSH constructor
// Read parameters from database file
// Load the hash functions, close the database
// Optionally load the LSH tables into head-allocated lists in core 
G::G(char* filename, bool lshInCoreFlag):
  H(), // default base-class constructor call delays data-structure initialization 
  indexName(0),
  lshHeader(0),
  calling_instance(0),
  add_point_callback(0)
{
  int dbfid = unserialize_lsh_header(filename);
  indexName = new char[O2_INDEX_MAXSTR];
  strncpy(indexName, filename, O2_INDEX_MAXSTR); // COPY THE CONTENTS TO THE NEW POINTER
  H::initialize_lsh_functions(); // Base-class data-structure initialization
  unserialize_lsh_functions(dbfid); // populate with on-disk hashfunction values

  // Format1 only needs unserializing if specifically requested
  if(!(lshHeader->flags&O2_SERIAL_FILEFORMAT2) && lshInCoreFlag){
    unserialize_lsh_hashtables_format1(dbfid);
  }

  // Format2 always needs unserializing
  if(lshHeader->flags&O2_SERIAL_FILEFORMAT2 && lshInCoreFlag){
    unserialize_lsh_hashtables_format2(dbfid);
  }

  close(dbfid);}

G::~G(){
  delete lshHeader;
}

// single point insertion; inserted values are hash value and pointID
Uns32T G::insert_point(vector<float>& v, Uns32T pp){
  Uns32T collisionCount = 0;
  H::p = pp;
  if(H::maxp && pp<=H::maxp)
    error("points must be indexed in strict ascending order", "LSH::insert_point(vector<float>&, Uns32T pointID)");
  H::maxp=pp; // Store highest pointID in database
  H::compute_hash_functions( v );
  for(Uns32T j = 0 ; j < H::L ; j++ ){ // insertion
    H::generate_hash_keys( *( H::g + j ), *( H::r1 + j ), *( H::r2 + j ) ); 
    collisionCount += bucket_insert_point( *(h + j) + t1 );
  }
  return collisionCount;
}


// batch insert for a point set
// inserted values are vector hash value and pointID starting at basePointID
void G::insert_point_set(vector<vector<float> >& vv, Uns32T basePointID){
  for(Uns32T point=0; point<vv.size(); point++)
    insert_point(vv[point], basePointID+point);
}

// point retrieval routine
void G::retrieve_point(vector<float>& v, Uns32T qpos, ReporterCallbackPtr add_point, void* caller){
  calling_instance = caller;
  add_point_callback = add_point;
  H::compute_hash_functions( v );
  for(Uns32T j = 0 ; j < H::L ; j++ ){
    H::generate_hash_keys( *( H::g + j ), *( H::r1 + j ), *( H::r2 + j ) ); 
    if( bucket* bPtr = *(get_bucket(j) + get_t1()) )
#ifdef LSH_BLOCK_FULL_ROWS
      bucket_chain_point( bPtr->next, qpos);
#else
    bucket_chain_point( bPtr , qpos);
#endif
  }
}

void G::retrieve_point_set(vector<vector<float> >& vv, ReporterCallbackPtr add_point, void* caller){
  for(Uns32T qpos = 0 ; qpos < vv.size() ; qpos++ )
    retrieve_point(vv[qpos], qpos, add_point, caller);
}

// export lsh tables to table structure on disk
// 
// LSH TABLE STRUCTURE
// ---header 64 bytes ---
// [magic #tables #rows #cols elementSize databaseSize version flags dim #funs 0 0 0 0 0 0]
//
// ---random projections L x k x d float ---
// A[0][0][0] A[0][0][1] ... A[0][0][d-1]
// A[0][1][0] A[0][1][1] ... A[1][1][d-1]
// ...
// A[0][K-1][0] A[0][1][1] ... A[0][k-1][d-1]
// ...
// ...
// A[L-1][0][0] A[M-1][0][1] ... A[L-1][0][d-1]
// A[L-1][1][0] A[M-1][1][1] ... A[L-1][1][d-1]
// ...
// A[L-1][k-1][0] A[M-1][1][1] ... A[L-1][k-1][d-1]
//
// ---bias L x k float ---
// b[0][0] b[0][1] ... b[0][k-1]
// b[1][0] b[1][1] ... b[1][k-1]
// ...
// b[L-1][0] b[L-1][1] ... b[L-1][k-1]
//
// ---random r1 L x k float ---
// r1[0][0] r1[0][1] ... r1[0][k-1]
// r1[1][0] r1[1][1] ... r1[1][k-1]
// ...
// r1[L-1][0] r1[L-1][1] ... r1[L-1][k-1]
//
// ---random r2 L x k float ---
// r2[0][0] r2[0][1] ... r2[0][k-1]
// r2[1][0] r2[1][1] ... r2[1][k-1]
// ...
// r2[L-1][0] r2[L-1][1] ... r2[L-1][k-1]
//
// ******* HASHTABLES FORMAT1 (optimized for LSH_ON_DISK retrieval) *******
// ---hash table 0: N x C x 8 ---
// [t2 pointID][t2 pointID]...[t2 pointID]
// [t2 pointID][t2 pointID]...[t2 pointID]
// ...
// [t2 pointID][t2 pointID]...[t2 pointID]
//
// ---hash table 1: N x C x 8 ---
// [t2 pointID][t2 pointID]...[t2 pointID]
// [t2 pointID][t2 pointID]...[t2 pointID]
// ...
// [t2 pointID][t2 pointID]...[t2 pointID]
// 
// ...
// 
// ---hash table L-1: N x C x 8 ---
// [t2 pointID][t2 pointID]...[t2 pointID]
// [t2 pointID][t2 pointID]...[t2 pointID]
// ...
// [t2 pointID][t2 pointID]...[t2 pointID]
//
// ******* HASHTABLES FORMAT2 (optimized for LSH_IN_CORE retrieval) *******
//
// State machine controlled by regular expression.
// legend:
//
// O2_SERIAL_TOKEN_T1 = 0xFFFFFFFCU
// O2_SERIAL_TOKEN_T2 = 0xFFFFFFFDU
// O2_SERIAL_TOKEN_ENDTABLE = 0xFFFFFFFEU
//
// T1 - T1 hash token 
// t1 - t1 hash key (t1 range 0..2^29-1) 
// T2 - T2 token
// t2 - t2 hash key (range 1..2^32-6)
// p  - point identifier (range 0..2^32-1)
// E  - end hash table token
// {...} required arguments
// [...] optional arguments
// *  - match zero or more occurences
// +  - match one or more occurences
// {...}^L - repeat argument L times
//
// FORMAT2 Regular expression:
// { [T1 t1 [T2 t2 p+]+ ]* E }^L
//

// Serial header constructors
SerialHeader::SerialHeader(){;}
SerialHeader::SerialHeader(float W, Uns32T L, Uns32T N, Uns32T C, Uns32T k, Uns32T d, float r, Uns32T p, Uns32T FMT, Uns32T pc):
  lshMagic(O2_SERIAL_MAGIC),
  binWidth(W),
  numTables(L),
  numRows(N),
  numCols(C),
  elementSize(O2_SERIAL_ELEMENT_SIZE),
  version(O2_SERIAL_VERSION),  
  size(0), // we are deprecating this value
  flags(FMT),
  dataDim(d),
  numFuns(k),
  radius(r),
  maxp(p),
  size_long((unsigned long long)L * align_up((unsigned long long)N * C * O2_SERIAL_ELEMENT_SIZE, get_page_logn())   // hash tables
	    + align_up(O2_SERIAL_HEADER_SIZE + // header + hash functions
		  (unsigned long long)L*k*( sizeof(float)*d+2*sizeof(Uns32T)+sizeof(float)),get_page_logn())),
  pointCount(pc){
  
  if(FMT==O2_SERIAL_FILEFORMAT2)
    size_long = (unsigned long long)align_up(O2_SERIAL_HEADER_SIZE 
	     + (unsigned long long)L*k*(sizeof(float)*d+2+sizeof(Uns32T)
	      +sizeof(float)) + (unsigned long long)pc*16UL,get_page_logn());
} // header

float* G::get_serial_hashfunction_base(char* db){
  if(db&&lshHeader)
    return (float*)(db+O2_SERIAL_HEADER_SIZE); 
  else return NULL;
}

SerialElementT* G::get_serial_hashtable_base(char* db){
  if(db&&lshHeader)
    return (SerialElementT*)(db+get_serial_hashtable_offset());
  else 
    return NULL;
}
  
Uns32T G::get_serial_hashtable_offset(){
  if(lshHeader)
    return align_up(O2_SERIAL_HEADER_SIZE + 
		    L*lshHeader->numFuns*( sizeof(float)*lshHeader->dataDim+2*sizeof(Uns32T)+sizeof(float)),get_page_logn());
  else
    return 0;
}

void G::serialize(char* filename, Uns32T serialFormat){
  int dbfid;
  char* db;
  int dbIsNew=0;

  // Check requested serialFormat
  if(!(serialFormat==O2_SERIAL_FILEFORMAT1 || serialFormat==O2_SERIAL_FILEFORMAT2))
    error("Unrecognized serial file format request: ", "serialize()");
 
  // Test to see if file exists
  if((dbfid = open (filename, O_RDONLY)) < 0)
    // If it doesn't, then create the file (CREATE)
    if(errno == ENOENT){
      // Create the file
      std::cout << "Creating new serialized LSH database:" << filename << "...";
      std::cout.flush();
      serial_create(filename, serialFormat);
      dbIsNew=1;
    }
    else
      // The file can't be opened
      error("Can't open the file", filename, "open");

  // Load the on-disk header into core
  dbfid = serial_open(filename, 1); // open for write
  db = serial_mmap(dbfid, O2_SERIAL_HEADER_SIZE, 1);// get database pointer
  serial_get_header(db);           // read header
  serial_munmap(db, O2_SERIAL_HEADER_SIZE); // drop mmap

  // Check compatibility of core and disk data structures
  if( !serial_can_merge(serialFormat) )
    error("Incompatible core and serial LSH, data structure dimensions mismatch.");
  
  // For new LSH databases write the hashfunctions
  if(dbIsNew)
    serialize_lsh_hashfunctions(dbfid);
  // Write the hashtables in the requested format
  if(serialFormat == O2_SERIAL_FILEFORMAT1)
    serialize_lsh_hashtables_format1(dbfid, !dbIsNew);
  else
    serialize_lsh_hashtables_format2(dbfid, !dbIsNew);

  if(!dbIsNew){
    db = serial_mmap(dbfid, O2_SERIAL_HEADER_SIZE, 1);// get database pointer
    //serial_get_header(db);           // read header
    cout << "maxp = " << H::maxp << endl;
    lshHeader->maxp=H::maxp;
    // Default to FILEFORMAT1
    if(!(lshHeader->flags&O2_SERIAL_FILEFORMAT2))
      lshHeader->flags|=O2_SERIAL_FILEFORMAT1;
    memcpy((char*)db, (char*)lshHeader, sizeof(SerialHeaderT));
    serial_munmap(db, O2_SERIAL_HEADER_SIZE); // drop mmap
  }

  serial_close(dbfid);
}

// Test to see if core structure and requested format is
// compatible with currently opened database
int G::serial_can_merge(Uns32T format){
  SerialHeaderT* that = lshHeader;
  if( (format==O2_SERIAL_FILEFORMAT2 && !that->flags&O2_SERIAL_FILEFORMAT2) 
      || (format!=O2_SERIAL_FILEFORMAT2 && that->flags&O2_SERIAL_FILEFORMAT2)
      || !( this->w == that->binWidth &&
	    this->L == that->numTables &&
	    this->N == that->numRows &&
	    this->k == that->numFuns &&
	    this->d == that->dataDim &&
	    sizeof(SerialElementT) == that->elementSize &&
	    this->radius == that->radius)){
    serial_print_header(format);
    return 0;
  }
  else
    return 1;
}

// Used as an error message for serial_can_merge()
void G::serial_print_header(Uns32T format){
  std::cout << "Fc:" << format << " Fs:" << lshHeader->flags << endl;
  std::cout << "Wc:" << w << " Ls:" << lshHeader->binWidth << endl;
  std::cout << "Lc:" << L << " Ls:" << lshHeader->numTables << endl;
  std::cout << "Nc:" << N << " Ns:" << lshHeader->numRows << endl;
  std::cout << "kc:" << k << " ks:" << lshHeader->numFuns << endl;
  std::cout << "dc:" << d << " ds:" << lshHeader->dataDim << endl;
  std::cout << "sc:" << sizeof(SerialElementT) << " ss:" << lshHeader->elementSize << endl;
  std::cout << "rc:" << this->radius << " rs:" << lshHeader->radius << endl;
}

int G::serialize_lsh_hashfunctions(int fid){
  float* pf;
  Uns32T *pu;
  Uns32T x,y,z;
  
  char* db = serial_mmap(fid, get_serial_hashtable_offset(), 1);// get database pointer  
  pf = get_serial_hashfunction_base(db);

  // HASH FUNCTIONS
  // Write the random projectors A[][][]
#ifdef USE_U_FUNCTIONS
  for( x = 0 ; x < H::m ; x++ )
    for( y = 0 ; y < H::k/2 ; y++ )
#else
      for( x = 0 ; x < H::L ; x++ )
	for( y = 0 ; y < H::k ; y++ )
#endif
	  for( z = 0 ; z < d ; z++ )
	    *pf++ = H::A[x][y][z];
  
  // Write the random biases b[][]
#ifdef USE_U_FUNCTIONS
  for( x = 0 ; x < H::m ; x++ )
    for( y = 0 ; y < H::k/2 ; y++ )
#else
      for( x = 0 ; x < H::L ; x++ )
	for( y = 0 ; y < H::k ; y++ )
#endif
	  *pf++ = H::b[x][y];
  
  pu = (Uns32T*)pf;
  
  // Write the Z projectors r1[][]
  for( x = 0 ; x < H::L ; x++)
    for( y = 0 ; y < H::k ; y++)
      *pu++ = H::r1[x][y];
  
  // Write the Z projectors r2[][]
  for( x = 0 ; x < H::L ; x++)
    for( y = 0; y < H::k ; y++)
      *pu++ = H::r2[x][y];  
  
  serial_munmap(db, get_serial_hashtable_offset());
  return 1;
}

int G::serialize_lsh_hashtables_format1(int fid, int merge){
  SerialElementT *pe, *pt;
  Uns32T x,y;

  if( merge && !serial_can_merge(O2_SERIAL_FILEFORMAT1) )
    error("Cannot merge core and serial LSH, data structure dimensions mismatch.");

  Uns32T hashTableSize=sizeof(SerialElementT)*lshHeader->numRows*lshHeader->numCols;  
  Uns32T colCount, meanColCount, colCountN, maxColCount, minColCount;
  // Write the hash tables
  for( x = 0 ; x < H::L ; x++ ){
    std::cout << (merge ? "merging":"writing") << " hash table " << x << " FORMAT1...";
    std::cout.flush();
    // memory map a single hash table for sequential access
    // Align each hash table to page boundary
    char* dbtable = serial_mmap(fid, hashTableSize, 1, 
				align_up(get_serial_hashtable_offset()+x*hashTableSize, get_page_logn()));
#ifdef __CYGWIN__
    // No madvise in CYGWIN
#else
    if(madvise(dbtable, hashTableSize, MADV_SEQUENTIAL)<0)
      error("could not advise hashtable memory","","madvise");
#endif
    maxColCount=0;
    minColCount=O2_SERIAL_MAX_COLS;
    meanColCount=0;
    colCountN=0;
    pt=(SerialElementT*)dbtable;
    for( y = 0 ;  y < H::N ; y++ ){
      // Move disk pointer to beginning of row
      pe=pt+y*lshHeader->numCols;
      
      colCount=0;
      if(bucket* bPtr = h[x][y])
	if(merge)
#ifdef LSH_BLOCK_FULL_ROWS
	  serial_merge_hashtable_row_format1(pe, bPtr->next, colCount); // skip collision counter bucket
	else
	  serial_write_hashtable_row_format1(pe, bPtr->next, colCount); // skip collision counter bucket
#else
      serial_merge_hashtable_row_format1(pe, bPtr, colCount);
      else
	serial_write_hashtable_row_format1(pe, bPtr, colCount);
#endif
      if(colCount){
	if(colCount<minColCount)
	  minColCount=colCount;
	if(colCount>maxColCount)
	  maxColCount=colCount;
	meanColCount+=colCount;
	colCountN++;
      }
    }
    if(colCountN)
      std::cout << "#rows with collisions =" << colCountN << ", mean = " << meanColCount/(float)colCountN 
		<< ", min = " << minColCount << ", max = " << maxColCount 
		<< endl;
    serial_munmap(dbtable, hashTableSize);
  }
  
  // We're done writing
  return 1;
}

void G::serial_merge_hashtable_row_format1(SerialElementT* pr, bucket* b, Uns32T& colCount){
  while(b && b->t2!=IFLAG){
    SerialElementT*pe=pr; // reset disk pointer to beginning of row
    serial_merge_element_format1(pe, b->snext, b->t2, colCount);  
    b=b->next;
  }
}

void G::serial_merge_element_format1(SerialElementT* pe, sbucket* sb, Uns32T t2, Uns32T& colCount){
  while(sb){        
    if(colCount==lshHeader->numCols){
      std::cout << "!point-chain full " << endl;
      return;
    }
    Uns32T c=0;
    // Merge collision chains 
    while(c<lshHeader->numCols){
      if( (pe+c)->hashValue==IFLAG){
	(pe+c)->hashValue=t2;
	(pe+c)->pointID=sb->pointID;
	colCount=c+1;
	if(c+1<lshHeader->numCols)
	  (pe+c+1)->hashValue=IFLAG;
	break;
      }
      c++;
    }
    sb=sb->snext;    
  }
  return;
}

void G::serial_write_hashtable_row_format1(SerialElementT*& pe, bucket* b, Uns32T& colCount){
  pe->hashValue=IFLAG;
  while(b && b->t2!=IFLAG){
    serial_write_element_format1(pe, b->snext, b->t2, colCount);
    b=b->next;
  }
}

void G::serial_write_element_format1(SerialElementT*& pe, sbucket* sb, Uns32T t2, Uns32T& colCount){
  while(sb){        
    if(colCount==lshHeader->numCols){
      std::cout << "!point-chain full "	<< endl;
      return;
    }
    pe->hashValue=t2;
    pe->pointID=sb->pointID;
    pe++;
    colCount++;
    sb=sb->snext;  
  }
  pe->hashValue=IFLAG;
  return;
}

int G::serialize_lsh_hashtables_format2(int fid, int merge){
  Uns32T x,y;

  if( merge && !serial_can_merge(O2_SERIAL_FILEFORMAT2) )
    error("Cannot merge core and serial LSH, data structure dimensions mismatch.");

  // We must pereform FORMAT2 merges in core
  if(merge)
    unserialize_lsh_hashtables_format2(fid);

  Uns32T colCount, meanColCount, colCountN, maxColCount, minColCount, t1;
  lseek(fid, get_serial_hashtable_offset(), SEEK_SET);

  // Write the hash tables
  for( x = 0 ; x < H::L ; x++ ){
    std::cout << (merge ? "merging":"writing") << " hash table " << x << " FORMAT2...";
    std::cout.flush();
    maxColCount=0;
    minColCount=O2_SERIAL_MAX_COLS;
    meanColCount=0;
    colCountN=0;
    for( y = 0 ;  y < H::N ; y++ ){
      colCount=0;
      if(bucket* bPtr = h[x][y]){
	// Check for empty row (even though row was allocated)
#ifdef LSH_BLOCK_FULL_ROWS
	if(bPtr->next->t2==IFLAG){
	  close(fid);
	  error("b->next->t2==IFLAG","serialize_lsh_hashtables_format2()");
	}
#else
	if(bPtr->t2==IFLAG){
	  close(fid);
	  error("b->t2==IFLAG","serialize_lsh_hashtables_format2()");
	}
#endif
	t1 = O2_SERIAL_TOKEN_T1;
	if( write(fid, &t1, sizeof(Uns32T)) != sizeof(Uns32T) ){
	  close(fid);
	  error("write error in serial_write_hashtable_format2() [T1]");
	}
	t1 = y;
	if( write(fid, &t1, sizeof(Uns32T)) != sizeof(Uns32T) ){
	  close(fid);
	  error("write error in serial_write_hashtable_format2() [t1]");
	}
#ifdef LSH_BLOCK_FULL_ROWS
	serial_write_hashtable_row_format2(fid, bPtr->next, colCount); // skip collision counter bucket
#else
      serial_write_hashtable_row_format2(fid, bPtr, colCount);
#endif
      }
      if(colCount){
	if(colCount<minColCount)
	  minColCount=colCount;
	if(colCount>maxColCount)
	  maxColCount=colCount;
	meanColCount+=colCount;
	colCountN++;
      }
    }
    // Write END of table marker
    t1 = O2_SERIAL_TOKEN_ENDTABLE;
    if( write(fid, &t1, sizeof(Uns32T)) != sizeof(Uns32T) ){
      close(fid);
      error("write error in serial_write_hashtable_format2() [end]");
    }

    if(colCountN)
      std::cout << "#rows with collisions =" << colCountN << ", mean = " << meanColCount/(float)colCountN 
		<< ", min = " << minColCount << ", max = " << maxColCount 
		<< endl;
  }
  
  // We're done writing
  return 1;
}

void G::serial_write_hashtable_row_format2(int fid, bucket* b, Uns32T& colCount){
  while(b && b->t2!=IFLAG){
    if(!b->snext){
      close(fid);
      error("Empty collision chain in serial_write_hashtable_row_format2()");
    }
    t2 = O2_SERIAL_TOKEN_T2;
    if( write(fid, &t2, sizeof(Uns32T)) != sizeof(Uns32T) ){
      close(fid);
      error("write error in serial_write_hashtable_row_format2()");
    }
    t2 = b->t2;
    if( write(fid, &t2, sizeof(Uns32T)) != sizeof(Uns32T) ){
      close(fid);
      error("write error in serial_write_hashtable_row_format2()");
    }
    serial_write_element_format2(fid, b->snext, colCount);
    b=b->next;
  }
}

void G::serial_write_element_format2(int fid, sbucket* sb, Uns32T& colCount){
  while(sb){
    if(write(fid, &sb->pointID, sizeof(Uns32T))!=sizeof(Uns32T)){
      close(fid);
      error("Write error in serial_write_element_format2()");
    }
    colCount++;
    sb=sb->snext;  
  }
}


int G::serial_create(char* filename, Uns32T FMT){
  return serial_create(filename, w, L, N, C, k, d, FMT);
}


int G::serial_create(char* filename, float binWidth, Uns32T numTables, Uns32T numRows, Uns32T numCols, 
		     Uns32T numFuns, Uns32T dim, Uns32T FMT){

  if(numTables > O2_SERIAL_MAX_TABLES || numRows > O2_SERIAL_MAX_ROWS 
     || numCols > O2_SERIAL_MAX_COLS || numFuns > O2_SERIAL_MAX_FUNS
     || dim>O2_SERIAL_MAX_DIM){
    error("LSH parameters out of bounds for serialization");
  }

  int dbfid;
  if ((dbfid = open (filename, O_RDWR|O_CREAT|O_EXCL, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH)) < 0)    
    error("Can't create serial file", filename, "open");
  get_lock(dbfid, 1);
  
  // Make header first to get size of serialized database
  lshHeader = new SerialHeaderT(binWidth, numTables, numRows, numCols, numFuns, dim, radius, maxp, FMT, pointCount);  

  cout << "file size: <=" << lshHeader->get_size()/1024UL << "KB" << endl;
  if(lshHeader->get_size()>O2_SERIAL_MAXFILESIZE)
    error("Maximum size of LSH file exceded: > 4000MB");

  // go to the location corresponding to the last byte
  if (lseek (dbfid, lshHeader->get_size() - 1, SEEK_SET) == -1)
    error("lseek error in db file", "", "lseek");
  
  // write a dummy byte at the last location
  if (write (dbfid, "", 1) != 1)
    error("write error", "", "write");
  
  char* db = serial_mmap(dbfid, O2_SERIAL_HEADER_SIZE, 1);
  
  memcpy (db, lshHeader, O2_SERIAL_HEADER_SIZE);
  
  serial_munmap(db, O2_SERIAL_HEADER_SIZE);
  
  close(dbfid);

  std::cout << "done initializing tables." << endl;

  return 1;  
}

char* G::serial_mmap(int dbfid, Uns32T memSize, Uns32T forWrite, off_t offset){
  char* db;
  if(forWrite){
    if ((db = (char*) mmap(0, memSize, PROT_READ | PROT_WRITE,
			   MAP_SHARED, dbfid, offset)) == (caddr_t) -1)
      error("mmap error in request for writable serialized database", "", "mmap");  
  }
  else if ((db = (char*) mmap(0, memSize, PROT_READ, MAP_SHARED, dbfid, offset)) == (caddr_t) -1)
    error("mmap error in read-only serialized database", "", "mmap");  
      
  return db;
}

SerialHeaderT* G::serial_get_header(char* db){
  lshHeader = new SerialHeaderT();
  memcpy((char*)lshHeader, db, sizeof(SerialHeaderT));  

  if(lshHeader->lshMagic!=O2_SERIAL_MAGIC)
    error("Not an LSH database file");

  return lshHeader;
}

void G::serial_munmap(char* db, Uns32T N){
  munmap(db, N);
}

int G::serial_open(char* filename, int writeFlag){
  int dbfid;
  if(writeFlag){
    if ((dbfid = open (filename, O_RDWR)) < 0)    
      error("Can't open serial file for read/write", filename, "open");
    get_lock(dbfid, writeFlag);
  }
  else{
    if ((dbfid = open (filename, O_RDONLY)) < 0)
      error("Can't open serial file for read", filename, "open");
    get_lock(dbfid, 0);
  }

  return dbfid;  
}

void G::serial_close(int dbfid){

  release_lock(dbfid);
  close(dbfid);
}

int G::unserialize_lsh_header(char* filename){

  int dbfid;
  char* db;
  // Test to see if file exists
  if((dbfid = open (filename, O_RDONLY)) < 0)
    error("Can't open the file", filename, "open");
  close(dbfid);
  dbfid = serial_open(filename, 0); // open for read
  db = serial_mmap(dbfid, O2_SERIAL_HEADER_SIZE, 0);// get database pointer
  serial_get_header(db);           // read header
  serial_munmap(db, O2_SERIAL_HEADER_SIZE); // drop mmap

  // Unserialize header parameters
  H::L = lshHeader->numTables;
  H::m = (Uns32T)( (1.0 + sqrt(1 + 8.0*(int)H::L)) / 2.0);
  H::N = lshHeader->numRows;
  H::C = lshHeader->numCols;
  H::k = lshHeader->numFuns;
  H::d = lshHeader->dataDim;
  H::w = lshHeader->binWidth;
  H::radius = lshHeader->radius;
  H::maxp = lshHeader->maxp;
  H::pointCount = lshHeader->pointCount;

  return dbfid;
}

// unserialize the LSH parameters
// we leave the LSH tree on disk as a flat file
// it is this flat file that we search by memory mapping
void G::unserialize_lsh_functions(int dbfid){
  Uns32T j, kk;
  float* pf;
  Uns32T* pu;

  // Load the hash functions into core
  char* db = serial_mmap(dbfid, get_serial_hashtable_offset(), 0);// get database pointer again  
  
  pf = get_serial_hashfunction_base(db);
  
#ifdef USE_U_FUNCTIONS
  for( j = 0 ; j < H::m ; j++ ){ // L functions gj(v)
    for( kk = 0 ; kk < H::k/2 ; kk++ ){ // Normally distributed hash functions
#else
      for( j = 0 ; j < H::L ; j++ ){ // L functions gj(v)
	for( kk = 0 ; kk < H::k ; kk++ ){ // Normally distributed hash functions
#endif
	  for(Uns32T i = 0 ; i < H::d ; i++ )
	    H::A[j][kk][i] = *pf++; // Normally distributed random vectors
	}
      }
#ifdef USE_U_FUNCTIONS
      for( j = 0 ; j < H::m ; j++ ) // biases b
	for( kk = 0 ; kk < H::k/2 ; kk++ )
#else
	  for( j = 0 ; j < H::L ; j++ ) // biases b
	    for( kk = 0 ; kk < H::k ; kk++ )
#endif
	      H::b[j][kk] = *pf++;
      
      pu = (Uns32T*)pf;
      for( j = 0 ; j < H::L ; j++ ) // Z projectors r1
	for( kk = 0 ; kk < H::k ; kk++ )
	  H::r1[j][kk] = *pu++;
      
      for( j = 0 ; j < H::L ; j++ ) // Z projectors r2
	for( kk = 0 ; kk < H::k ; kk++ )
	  H::r2[j][kk] = *pu++;

      serial_munmap(db, get_serial_hashtable_offset());
}

void G::unserialize_lsh_hashtables_format1(int fid){
  SerialElementT *pe, *pt;
  Uns32T x,y;
  Uns32T hashTableSize=sizeof(SerialElementT)*lshHeader->numRows*lshHeader->numCols;
  // Read the hash tables into core
  for( x = 0 ; x < H::L ; x++ ){
    // memory map a single hash table
    // Align each hash table to page boundary
    char* dbtable = serial_mmap(fid, hashTableSize, 0, 
				align_up(get_serial_hashtable_offset()+x*hashTableSize, get_page_logn()));
#ifdef __CYGWIN__
    // No madvise in CYGWIN
#else
    if(madvise(dbtable, hashTableSize, MADV_SEQUENTIAL)<0)
      error("could not advise hashtable memory","","madvise");    
#endif
    pt=(SerialElementT*)dbtable;
    for( y = 0 ; y < H::N ; y++ ){
      // Move disk pointer to beginning of row
      pe=pt+y*lshHeader->numCols;
      unserialize_hashtable_row_format1(pe, h[x]+y);
#ifdef __LSH_DUMP_CORE_TABLES__
      printf("S[%d,%d]", x, y);
      serial_bucket_dump(pe);
      printf("C[%d,%d]", x, y);
      dump_hashtable_row(h[x][y]);
#endif      
    }
    serial_munmap(dbtable, hashTableSize);
  }  
}

void G::unserialize_hashtable_row_format1(SerialElementT* pe, bucket** b){
  Uns32T colCount = 0;  
  while(colCount!=lshHeader->numCols && pe->hashValue !=IFLAG){
    H::p = pe->pointID; // current point ID      
    t2 = pe->hashValue;
    bucket_insert_point(b);
    pe++;
    colCount++;
  }
}
 
void G::unserialize_lsh_hashtables_format2(int fid){
  Uns32T x=0,y=0;

  // Seek to hashtable base offset
  if(lseek(fid, get_serial_hashtable_offset(), SEEK_SET)!=get_serial_hashtable_offset()){
    close(fid);
    error("Seek error in unserialize_lsh_hashtables_format2");
  }

  // Read the hash tables into core (structure is given in header) 
  while( x < H::L){
    if(read(fid, &(H::t1), sizeof(Uns32T))!=sizeof(Uns32T)){
      close(fid);
      error("Read error","unserialize_lsh_hashtables_format2()");
    }
    if(H::t1==O2_SERIAL_TOKEN_ENDTABLE)
      x++; // End of table
    else
      while(y < H::N){
	// Read a row and move file pointer to beginning of next row or _bittable
	if(!(H::t1==O2_SERIAL_TOKEN_T1)){
	  close(fid);
	  error("State matchine error T1","unserialize_lsh_hashtables_format2()");
	}
	if(read(fid, &(H::t1), sizeof(Uns32T))!=sizeof(Uns32T)){
	  close(fid);
	  error("Read error: t1","unserialize_lsh_hashtables_format2()");
	}
	y = H::t1;
	if(y>=H::N){
	  close(fid);
	  error("Unserialized hashtable row pointer out of range","unserialize_lsh_hashtables_format2()");
	}
	Uns32T token = unserialize_hashtable_row_format2(fid, h[x]+y);
	
#ifdef __LSH_DUMP_CORE_TABLES__
	printf("C[%d,%d]", x, y);
	dump_hashtable_row(h[x][y]);
#endif
	// Check that token is valid
	if( !(token==O2_SERIAL_TOKEN_T1 || token==O2_SERIAL_TOKEN_ENDTABLE) ){
	  close(fid);
	  error("State machine error end of row/table", "unserialize_lsh_hashtables_format2()");
	}
	// Check for end of table flag
	if(token==O2_SERIAL_TOKEN_ENDTABLE){
	  x++;
	  break;
	}
	// Check for new row flag
	if(token==O2_SERIAL_TOKEN_T1)
	  H::t1 = token;
      }
  }
}
 
Uns32T G::unserialize_hashtable_row_format2(int fid, bucket** b){
  bool pointFound = false;
  if(read(fid, &(H::t2), sizeof(Uns32T)) != sizeof(Uns32T)){
    close(fid);
    error("Read error T2 token","unserialize_hashtable_row_format2");
  }
  if( !(H::t2==O2_SERIAL_TOKEN_ENDTABLE || H::t2==O2_SERIAL_TOKEN_T2)){
    close(fid);
    error("State machine error: expected E or T2");
  }
  while(!(H::t2==O2_SERIAL_TOKEN_ENDTABLE || H::t2==O2_SERIAL_TOKEN_T1)){
    pointFound=false;
    // Check for T2 token
    if(H::t2!=O2_SERIAL_TOKEN_T2){
      close(fid);
      error("State machine error T2 token", "unserialize_hashtable_row_format2()");
    }
    // Read t2 value
    if(read(fid, &(H::t2), sizeof(Uns32T)) != sizeof(Uns32T)){
      close(fid);
      error("Read error t2","unserialize_hashtable_row_format2");
    }
    if(read(fid, &(H::p), sizeof(Uns32T)) != sizeof(Uns32T)){
      close(fid);
      error("Read error H::p","unserialize_hashtable_row_format2");
    }
    while(!(H::p==O2_SERIAL_TOKEN_ENDTABLE || H::p==O2_SERIAL_TOKEN_T1 || H::p==O2_SERIAL_TOKEN_T2 )){
      pointFound=true;
      bucket_insert_point(b);
      if(read(fid, &(H::p), sizeof(Uns32T)) != sizeof(Uns32T)){
	close(fid);
	error("Read error H::p","unserialize_hashtable_row_format2");
      }
    }
    if(!pointFound)
      error("State machine error: point", "unserialize_hashtable_row_format2()");    
    H::t2 = H::p; // Copy last found token to t2
  }  
  return H::t2; // holds current token
}

void G::dump_hashtable_row(bucket* p){
  while(p && p->t2!=IFLAG){
    sbucket* sbp = p->snext;
    while(sbp){
      printf("(%0X,%u)", p->t2, sbp->pointID);
      fflush(stdout);
      sbp=sbp->snext;
    }      
    p=p->next;
  }
  printf("\n");
}


// G::serial_retrieve_point( ... )
// retrieves (pointID) from a serialized LSH database
//
// inputs:
// filename - file name of serialized LSH database
//       vv - query point set
//
// outputs:
// inserts retrieved points into add_point() callback method
void G::serial_retrieve_point_set(char* filename, vector<vector<float> >& vv, ReporterCallbackPtr add_point, void* caller)
{
  int dbfid = serial_open(filename, 0); // open for read
  char* dbheader = serial_mmap(dbfid, O2_SERIAL_HEADER_SIZE, 0);// get database pointer
  serial_get_header(dbheader);           // read header
  serial_munmap(dbheader, O2_SERIAL_HEADER_SIZE); // drop header mmap

  if((lshHeader->flags & O2_SERIAL_FILEFORMAT2)){
    close(dbfid);
    error("serial_retrieve_point_set is for SERIAL_FILEFORMAT1 only");
  }

  // size of each hash table
  Uns32T hashTableSize=sizeof(SerialElementT)*lshHeader->numRows*lshHeader->numCols;
  calling_instance = caller; // class instance variable used in ...bucket_chain_point()
  add_point_callback = add_point;

  for(Uns32T j=0; j<L; j++){
    // memory map a single hash table for random access
    char* db = serial_mmap(dbfid, hashTableSize, 0, 
			   align_up(get_serial_hashtable_offset()+j*hashTableSize,get_page_logn()));
#ifdef __CYGWIN__
    // No madvise in CYGWIN
#else
    if(madvise(db, hashTableSize, MADV_RANDOM)<0)
      error("could not advise local hashtable memory","","madvise");
#endif
    SerialElementT* pe = (SerialElementT*)db ;
    for(Uns32T qpos=0; qpos<vv.size(); qpos++){
      H::compute_hash_functions(vv[qpos]);
      H::generate_hash_keys(*(g+j),*(r1+j),*(r2+j)); 
      serial_bucket_chain_point(pe+t1*lshHeader->numCols, qpos); // Point to correct row
    }
    serial_munmap(db, hashTableSize); // drop hashtable mmap
  }  
  serial_close(dbfid);    
}

void G::serial_retrieve_point(char* filename, vector<float>& v, Uns32T qpos, ReporterCallbackPtr add_point, void* caller){
  int dbfid = serial_open(filename, 0); // open for read
  char* dbheader = serial_mmap(dbfid, O2_SERIAL_HEADER_SIZE, 0);// get database pointer
  serial_get_header(dbheader);           // read header
  serial_munmap(dbheader, O2_SERIAL_HEADER_SIZE); // drop header mmap

  if((lshHeader->flags & O2_SERIAL_FILEFORMAT2)){
    close(dbfid);
    error("serial_retrieve_point is for SERIAL_FILEFORMAT1 only");
  }

  // size of each hash table
  Uns32T hashTableSize=sizeof(SerialElementT)*lshHeader->numRows*lshHeader->numCols;
  calling_instance = caller;
  add_point_callback = add_point;
  H::compute_hash_functions(v);
  for(Uns32T j=0; j<L; j++){
    // memory map a single hash table for random access
    char* db = serial_mmap(dbfid, hashTableSize, 0, 
			   align_up(get_serial_hashtable_offset()+j*hashTableSize,get_page_logn()));
#ifdef __CYGWIN__
    // No madvise in CYGWIN
#else
    if(madvise(db, hashTableSize, MADV_RANDOM)<0)
      error("could not advise local hashtable memory","","madvise");
#endif
    SerialElementT* pe = (SerialElementT*)db ;
    H::generate_hash_keys(*(g+j),*(r1+j),*(r2+j)); 
    serial_bucket_chain_point(pe+t1*lshHeader->numCols, qpos); // Point to correct row
    serial_munmap(db, hashTableSize); // drop hashtable mmap
  }  
  serial_close(dbfid);    
}

void G::serial_dump_tables(char* filename){
  int dbfid = serial_open(filename, 0); // open for read
  char* dbheader = serial_mmap(dbfid, O2_SERIAL_HEADER_SIZE, 0);// get database pointer
  serial_get_header(dbheader);           // read header
  serial_munmap(dbheader, O2_SERIAL_HEADER_SIZE); // drop header mmap
  Uns32T hashTableSize=sizeof(SerialElementT)*lshHeader->numRows*lshHeader->numCols;
  for(Uns32T j=0; j<L; j++){
    // memory map a single hash table for random access
    char* db = serial_mmap(dbfid, hashTableSize, 0, 
			   align_up(get_serial_hashtable_offset()+j*hashTableSize,get_page_logn()));
#ifdef __CYGWIN__
    // No madvise in CYGWIN
#else
    if(madvise(db, hashTableSize, MADV_SEQUENTIAL)<0)
      error("could not advise local hashtable memory","","madvise");
#endif
    SerialElementT* pe = (SerialElementT*)db ;
    printf("*********** TABLE %d ***************\n", j);
    fflush(stdout);
    int count=0;
    do{
      printf("[%d,%d]", j, count++);
      fflush(stdout);
      serial_bucket_dump(pe);
      pe+=lshHeader->numCols;
    }while(pe<(SerialElementT*)db+lshHeader->numRows*lshHeader->numCols);
  }

}

void G::serial_bucket_dump(SerialElementT* pe){
  SerialElementT* pend = pe+lshHeader->numCols;
  while( !(pe->hashValue==IFLAG || pe==pend ) ){
    printf("(%0X,%u)",pe->hashValue,pe->pointID);
    pe++;
  }
  printf("\n");
  fflush(stdout);
}

void G::serial_bucket_chain_point(SerialElementT* pe, Uns32T qpos){
  SerialElementT* pend = pe+lshHeader->numCols;
  while( !(pe->hashValue==IFLAG || pe==pend ) ){
    if(pe->hashValue==t2){ // new match
      add_point_callback(calling_instance, pe->pointID, qpos, radius);
    }
    pe++;
  }
}

void G::bucket_chain_point(bucket* p, Uns32T qpos){
  if(!p || p->t2==IFLAG)
    return;
  if(p->t2==t2){ // match
    sbucket_chain_point(p->snext, qpos); // add to reporter
  }  
  if(p->next){
    bucket_chain_point(p->next, qpos); // recurse
  }
}

void G::sbucket_chain_point(sbucket* p, Uns32T qpos){  
  add_point_callback(calling_instance, p->pointID, qpos, radius);
  if(p->snext){
    sbucket_chain_point(p->snext, qpos);
  }
}

void G::get_lock(int fd, bool exclusive) {
  struct flock lock;
  int status;  
  lock.l_type = exclusive ? F_WRLCK : F_RDLCK;
  lock.l_whence = SEEK_SET;
  lock.l_start = 0;
  lock.l_len = 0; /* "the whole file" */  
 retry:
  do {
    status = fcntl(fd, F_SETLKW, &lock);
  } while (status != 0 && errno == EINTR);  
  if (status) {
    if (errno == EAGAIN) {
      sleep(1);
      goto retry;
    } else {
      error("fcntl lock error", "", "fcntl");
    }
  }
}
 
void G::release_lock(int fd) {
   struct flock lock;
   int status;
   
   lock.l_type = F_UNLCK;
   lock.l_whence = SEEK_SET;
   lock.l_start = 0;
   lock.l_len = 0;
   
   status = fcntl(fd, F_SETLKW, &lock);

  if (status)
    error("fcntl unlock error", "", "fcntl");
}