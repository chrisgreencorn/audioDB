#include "audioDB.h"

bool audioDB::enough_per_file_space_free() {
  unsigned int fmaxfiles, tmaxfiles;
  unsigned int maxfiles;

  fmaxfiles = fileTableLength / O2_FILETABLE_ENTRY_SIZE;
  tmaxfiles = trackTableLength / O2_TRACKTABLE_ENTRY_SIZE;
  maxfiles = fmaxfiles > tmaxfiles ? tmaxfiles : fmaxfiles;
  return(dbH->numFiles < maxfiles);
}

bool audioDB::enough_data_space_free(off_t size) {
    return(dbH->timesTableOffset > dbH->dataOffset + dbH->length + size);
}

void audioDB::insert_data_vectors(off_t offset, void *buffer, size_t size) {
  if(lseek(dbfid, dbH->dataOffset + offset, SEEK_SET) == (off_t) -1) {
    error("error seeking to offset", "", "lseek");
  }
  CHECKED_WRITE(dbfid, buffer, size);
}

void audioDB::insert(const char* dbName, const char* inFile) {
  forWrite = true;
  initTables(dbName, inFile);

  if(dbH->flags & O2_FLAG_LARGE_ADB)
    error("Single-feature inserts not allowed with LARGE audioDB instances");

  if(!usingTimes && (dbH->flags & O2_FLAG_TIMES))
    error("Must use timestamps with timestamped database","use --times");

  if(!usingPower && (dbH->flags & O2_FLAG_POWER))
    error("Must use power with power-enabled database", dbName);

  if(!enough_per_file_space_free()) {
    error("Insert failed: no more room for metadata", inFile);
  }

  if(!enough_data_space_free(statbuf.st_size - sizeof(int))) {
    error("Insert failed: no more room in database", inFile);
  }

  if(!key)
    key=inFile;
  // Linear scan of filenames check for pre-existing feature
  unsigned alreadyInserted=0;
  for(unsigned k=0; k<dbH->numFiles; k++)
    if(strncmp(fileTable + k*O2_FILETABLE_ENTRY_SIZE, key, strlen(key)+1)==0){
      alreadyInserted=1;
      break;
    }

  if(alreadyInserted) {
    VERB_LOG(0, "key already exists in database; ignoring: %s\n", inFile);
    // FIXME: Do we need to munmap here (see below) ? MKC 18/08/08
    return;
  }
  
  // Make a track index table of features to file indexes
  unsigned numVectors = (statbuf.st_size-sizeof(int))/(sizeof(double)*dbH->dim);
  if(!numVectors) {
    VERB_LOG(0, "ignoring zero-length feature vector file: %s\n", key);

    // CLEAN UP
    munmap(indata,statbuf.st_size);
    munmap(db,dbH->dbSize);
    close(infid);
    return;
  }

  INSERT_FILETABLE_STRING(fileTable, key);

  off_t insertoffset = dbH->length;// Store current state

  // Check times status and insert times from file
  unsigned indexoffset = insertoffset/(dbH->dim*sizeof(double));
  double *timesdata = timesTable + 2*indexoffset;

  if(2*(indexoffset + numVectors) > timesTableLength) {
    error("out of space for times", key);
  }
  
  if (usingTimes) {
    insertTimeStamps(numVectors, timesFile, timesdata);
  }

  double *powerdata = powerTable + indexoffset;
  insertPowerData(numVectors, powerfd, powerdata);

  // Increment file count
  dbH->numFiles++;

  // Update Header information
  dbH->length+=(statbuf.st_size-sizeof(int));

  // Update track to file index map
  memcpy(trackTable + dbH->numFiles - 1, &numVectors, sizeof(unsigned));  

  insert_data_vectors(insertoffset, indata + sizeof(int), statbuf.st_size - sizeof(int));
  
  // Norm the vectors on input if the database is already L2 normed
  if(dbH->flags & O2_FLAG_L2NORM)
    unitNormAndInsertL2((double *)(indata + sizeof(int)), dbH->dim, numVectors, 1); // append

  // Report status
  status(dbName);
  VERB_LOG(0, "%s %s %u vectors %jd bytes.\n", COM_INSERT, dbName, numVectors, (intmax_t) (statbuf.st_size - sizeof(int)));

  // Copy the header back to the database
  memcpy (db, dbH, sizeof(dbTableHeaderT));  

  // CLEAN UP
  munmap(indata,statbuf.st_size);
  close(infid);
}

void audioDB::insertTimeStamps(unsigned numVectors, std::ifstream *timesFile, double *timesdata) {
  assert(usingTimes);

  unsigned numtimes = 0;

  if(!(dbH->flags & O2_FLAG_TIMES) && !dbH->numFiles) {
    dbH->flags=dbH->flags|O2_FLAG_TIMES;
  } else if(!(dbH->flags & O2_FLAG_TIMES)) {
    error("Timestamp file used with non-timestamped database", timesFileName);
  }
   
  if(!timesFile->is_open()) {
    error("problem opening times file on timestamped database", timesFileName);
  }

  double timepoint, next;
  *timesFile >> timepoint;
  if (timesFile->eof()) {
    error("no entries in times file", timesFileName);
  }
  numtimes++;
  do {
    *timesFile >> next;
    if (timesFile->eof()) {
      break;
    }
    numtimes++;
    timesdata[0] = timepoint;
    timepoint = (timesdata[1] = next);
    timesdata += 2;
  } while (numtimes < numVectors + 1);

  if (numtimes < numVectors + 1) {
    error("too few timepoints in times file", timesFileName);
  }

  *timesFile >> next;
  if (!timesFile->eof()) {
    error("too many timepoints in times file", timesFileName);
  }
}

void audioDB::insertPowerData(unsigned numVectors, int powerfd, double *powerdata) {
  if(usingPower){
    if (!(dbH->flags & O2_FLAG_POWER)) {
      error("Cannot insert power data on non-power DB", dbName);
    }
    
    int one;
    unsigned int count;
    
    count = read(powerfd, &one, sizeof(unsigned int));
    if (count != sizeof(unsigned int)) {
      error("powerfd read failed", "int", "read");
    }
    if (one != 1) {
      error("dimensionality of power file not 1", powerFileName);
    }
    
    // FIXME: should check that the powerfile is the right size for
    // this.  -- CSR, 2007-10-30
    count = read(powerfd, powerdata, numVectors * sizeof(double));
    if (count != numVectors * sizeof(double)) {
      error("powerfd read failed", "double", "read");
    }
  }
}

void audioDB::batchinsert(const char* dbName, const char* inFile) {

  forWrite = true;
  initDBHeader(dbName);

  // Treat large ADB instances differently
  if( dbH->flags & O2_FLAG_LARGE_ADB ){
    batchinsert_large_adb(dbName, inFile) ;
    return;
  }
    
  if(!key)
    key=inFile;
  std::ifstream *filesIn = 0;
  std::ifstream *keysIn = 0;
  std::ifstream* thisTimesFile = 0;
  int thispowerfd = 0;

  if(!(filesIn = new std::ifstream(inFile)))
    error("Could not open batch in file", inFile);
  if(key && key!=inFile)
    if(!(keysIn = new std::ifstream(key)))
      error("Could not open batch key file",key);
  
  if(!usingTimes && (dbH->flags & O2_FLAG_TIMES))
    error("Must use timestamps with timestamped database","use --times");

  if(!usingPower && (dbH->flags & O2_FLAG_POWER))
    error("Must use power with power-enabled database", dbName);

  unsigned totalVectors=0;
  char *thisFile = new char[MAXSTR];
  char *thisKey = 0;
  if (key && (key != inFile)) {
    thisKey = new char[MAXSTR];
  }
  char *thisTimesFileName = new char[MAXSTR];
  char *thisPowerFileName = new char[MAXSTR];

  std::set<std::string> s;

  for (unsigned k = 0; k < dbH->numFiles; k++) {
    s.insert(fileTable + k*O2_FILETABLE_ENTRY_SIZE);
  }

  do {
    filesIn->getline(thisFile,MAXSTR);
    if(key && key!=inFile) {
      keysIn->getline(thisKey,MAXSTR);
    } else {
      thisKey = thisFile;
    }
    if(usingTimes) {
      timesFile->getline(thisTimesFileName,MAXSTR);
    }
    if(usingPower) {
      powerFile->getline(thisPowerFileName, MAXSTR);
    }
    
    if(filesIn->eof()) {
      break;
    }
    initInputFile(thisFile);

    if(!enough_per_file_space_free()) {
      error("batchinsert failed: no more room for metadata", thisFile);
    }

    if(!enough_data_space_free(statbuf.st_size - sizeof(int))) {
      error("batchinsert failed: no more room in database", thisFile);
    }
    
    if(s.count(thisKey)) {
      VERB_LOG(0, "key already exists in database: %s\n", thisKey);
    } else {
      s.insert(thisKey);
      // Make a track index table of features to file indexes
      unsigned numVectors = (statbuf.st_size-sizeof(int))/(sizeof(double)*dbH->dim);
      if(!numVectors) {
        VERB_LOG(0, "ignoring zero-length feature vector file: %s\n", thisKey);
      }
      else{
	if(usingTimes){
	  if(timesFile->eof()) {
	    error("not enough timestamp files in timesList", timesFileName);
	  }
	  thisTimesFile = new std::ifstream(thisTimesFileName,std::ios::in);
	  if(!thisTimesFile->is_open()) {
	    error("Cannot open timestamp file", thisTimesFileName);
	  }
	  off_t insertoffset = dbH->length;
	  unsigned indexoffset = insertoffset / (dbH->dim*sizeof(double));
	  double *timesdata = timesTable + 2*indexoffset;
          if(2*(indexoffset + numVectors) > timesTableLength) {
            error("out of space for times", key);
          }
	  insertTimeStamps(numVectors, thisTimesFile, timesdata);
	  if(thisTimesFile)
	    delete thisTimesFile;
	}
        
        if (usingPower) {
          if(powerFile->eof()) {
            error("not enough power files in powerList", powerFileName);
          }
          thispowerfd = open(thisPowerFileName, O_RDONLY);
          if (thispowerfd < 0) {
            error("failed to open power file", thisPowerFileName);
          }
          off_t insertoffset = dbH->length;
          unsigned poweroffset = insertoffset / (dbH->dim * sizeof(double));
          double *powerdata = powerTable + poweroffset;
          insertPowerData(numVectors, thispowerfd, powerdata);
          if (0 < thispowerfd) {
            close(thispowerfd);
          }
        }

	INSERT_FILETABLE_STRING(fileTable, thisKey);

	off_t insertoffset = dbH->length;// Store current state

	// Increment file count
	dbH->numFiles++;  
  
	// Update Header information
	dbH->length+=(statbuf.st_size-sizeof(int));
  
	// Update track to file index map
	memcpy (trackTable+dbH->numFiles-1, &numVectors, sizeof(unsigned));  

	insert_data_vectors(insertoffset, indata + sizeof(int), statbuf.st_size - sizeof(int));
	
	// Norm the vectors on input if the database is already L2 normed
	if(dbH->flags & O2_FLAG_L2NORM)
	  unitNormAndInsertL2((double *)(indata + sizeof(int)), dbH->dim, numVectors, 1); // append
	
	totalVectors+=numVectors;

	// Copy the header back to the database
	memcpy (db, dbH, sizeof(dbTableHeaderT));  
      }
    }

    // CLEAN UP
    munmap(indata,statbuf.st_size);
    indata = NULL;
    close(infid);
    infid = 0;
  } while(!filesIn->eof());

  VERB_LOG(0, "%s %s %u vectors %ju bytes.\n", COM_BATCHINSERT, dbName, totalVectors, (intmax_t) (totalVectors * dbH->dim * sizeof(double)));

  delete [] thisPowerFileName;
  if(key && (key != inFile)) {
    delete [] thisKey;
  }
  delete [] thisFile;
  delete [] thisTimesFileName;
  
  delete filesIn;
  delete keysIn;

  // Report status
  status(dbName);
}


// BATCHINSERT_LARGE_ADB
//
// This method inserts file pointers into the ADB instance rather than the actual feature data
//
// This method is intended for databases that are large enough to only support indexed query
// So exhaustive searching across all feature vectors will not be performed
//
// We insert featureFileName, [powerFileName], [timesFileName]
//
// l2norms and power sequence sums are calculated on-the-fly at INDEX and --lsh_exact QUERY time
//
// LIMITS:
//
// We impose an upper limit of 1M keys, 1M featureFiles, 1M powerFiles and 1M timesFiles
//
void audioDB::batchinsert_large_adb(const char* dbName, const char* inFile) {

  if(!key)
    key=inFile;
  std::ifstream *filesIn = 0;
  std::ifstream *keysIn = 0;
  std::ifstream* thisTimesFile = 0;
  int thispowerfd = 0;

  if(!(filesIn = new std::ifstream(inFile)))
    error("Could not open batch in file", inFile);
  if(key && key!=inFile)
    if(!(keysIn = new std::ifstream(key)))
      error("Could not open batch key file",key);
  
  if(!usingTimes && (dbH->flags & O2_FLAG_TIMES))
    error("Must use timestamps with timestamped database","use --times");

  if(!usingPower && (dbH->flags & O2_FLAG_POWER))
    error("Must use power with power-enabled database", dbName);

  char *cwd = new char[PATH_MAX];

  if ((getcwd(cwd, PATH_MAX)) == 0) {
    error("error getting working directory", "", "getcwd");
  }

  unsigned totalVectors=0;
  char *thisFile = new char[MAXSTR];
  char *thisKey = 0;
  if (key && (key != inFile)) {
    thisKey = new char[MAXSTR];
  }
  char *thisTimesFileName = new char[MAXSTR];
  char *thisPowerFileName = new char[MAXSTR];

  std::set<std::string> s;

  for (unsigned k = 0; k < dbH->numFiles; k++) {
    s.insert(fileTable + k*O2_FILETABLE_ENTRY_SIZE);
  }

  do {
    filesIn->getline(thisFile,MAXSTR);
    if(key && key!=inFile) {
      keysIn->getline(thisKey,MAXSTR);
    } else {
      thisKey = thisFile;
    }
    if(usingTimes) {
      timesFile->getline(thisTimesFileName,MAXSTR);
    }
    if(usingPower) {
      powerFile->getline(thisPowerFileName, MAXSTR);
    }
    
    if(filesIn->eof()) {
      break;
    }
    
    initInputFile(thisFile, false);

    if(!enough_per_file_space_free()) {
      error("batchinsert failed: no more room for metadata", thisFile);
    }

    if(s.count(thisKey)) {
      VERB_LOG(0, "key already exists in database: %s\n", thisKey);
    } else {
      s.insert(thisKey);
      // Make a track index table of features to file indexes
      unsigned numVectors = (statbuf.st_size-sizeof(int))/(sizeof(double)*dbH->dim);
      if(!numVectors) {
        VERB_LOG(0, "ignoring zero-length feature vector file: %s\n", thisKey);
      }
      else{
	// Check that time-stamp file exists
	if(usingTimes){
	  if(timesFile->eof()) {
	    error("not enough timestamp files in timesList", timesFileName);
	  }
	  thisTimesFile = new std::ifstream(thisTimesFileName,std::ios::in);
	  if(!thisTimesFile->is_open()) {
	    error("Cannot open timestamp file", thisTimesFileName);
	  }
	  if(thisTimesFile)
	    delete thisTimesFile;
	}

	// Check that power file exists        
        if (usingPower) {
          if(powerFile->eof()) {
            error("not enough power files in powerList", powerFileName);
          }
          thispowerfd = open(thisPowerFileName, O_RDONLY);
          if (thispowerfd < 0) {
            error("failed to open power file", thisPowerFileName);
          }
          if (0 < thispowerfd) {
            close(thispowerfd);
          }
        }

	// persist links to the feature files for reading from filesystem later
	
	// Primary Keys
	INSERT_FILETABLE_STRING(fileTable, thisKey);

	if(*thisFile != '/') {
	  /* FIXME: MAXSTR and O2_FILETABLE_ENTRY_SIZE should probably
	     be the same thing.  Also, both are related to PATH_MAX,
	     which admittedly is not always defined or a
	     constant... */
	  char tmp[MAXSTR];
	  strncpy(tmp, thisFile, MAXSTR);
	  snprintf(thisFile, MAXSTR, "%s/%s", cwd, tmp);
	}
	// Feature Vector fileNames
	INSERT_FILETABLE_STRING(featureFileNameTable, thisFile);
	
	// Time Stamp fileNames
	if(usingTimes) {
	  if(*thisTimesFileName != '/') {
	    char tmp[MAXSTR];
	    strncpy(tmp, thisTimesFileName, MAXSTR);
	    snprintf(thisTimesFileName, MAXSTR, "%s/%s", cwd, tmp);
	  }
	  INSERT_FILETABLE_STRING(timesFileNameTable, thisTimesFileName);
	}

	// Power fileNames
	if(usingPower) {
	  if(*thisPowerFileName != '/') {
	    char tmp[MAXSTR];
	    strncpy(tmp, thisPowerFileName, MAXSTR);
	    snprintf(thisPowerFileName, MAXSTR, "%s/%s", cwd, tmp);
	  }
	  INSERT_FILETABLE_STRING(powerFileNameTable, thisPowerFileName);
	}

	// Increment file count
	dbH->numFiles++;  
  
	// Update Header information
	dbH->length+=(statbuf.st_size-sizeof(int));
  
	// Update track to file index map
	memcpy (trackTable+dbH->numFiles-1, &numVectors, sizeof(unsigned));  

	totalVectors+=numVectors;

	// Copy the header back to the database
	memcpy (db, dbH, sizeof(dbTableHeaderT));  
      }
    }
    // CLEAN UP
    if(indata)
      munmap(indata,statbuf.st_size);
    if(infid>0)
      close(infid);
  } while(!filesIn->eof());

  VERB_LOG(0, "%s %s %u vectors %ju bytes.\n", COM_BATCHINSERT, dbName, totalVectors, (intmax_t) (totalVectors * dbH->dim * sizeof(double)));

  delete [] thisPowerFileName;
  if(key && (key != inFile)) {
    delete [] thisKey;
  }
  delete [] thisFile;
  delete [] thisTimesFileName;
  
  delete filesIn;
  delete keysIn;

  // Report status
  status(dbName);
}
