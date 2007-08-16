// audioDBws.h -- web services interface to audioDB
//
//FIXME: this hard-coding of the service location might be right for
//its internal use at Goldsmiths (for now) but really isn't in
//general.  Find a way to bind this later (at install time?  Or maybe
//just require that the installer edit the resulting wsdl file?)
//
//gsoap adb service location: http://gibbons.doc.gold.ac.uk:20703/

typedef int xsd__int;
typedef double xsd__double;
typedef char* xsd__string;

// Supports result lists of arbitrary length
class adb__queryResult{
  int __sizeRlist;
  char **Rlist; // Maximum size of result list
  int __sizeDist;  
  double *Dist;
  int __sizeQpos;  
  int *Qpos;
  int __sizeSpos;
  int *Spos;
};

// Print the status of an existing adb database
int adb__status(xsd__string dbName, xsd__int &adbCreateResult);

// Query an existing adb database
int adb__query(xsd__string dbName, xsd__string qKey, xsd__string keyList, xsd__string timesFileName, xsd__int qType, xsd__int qPos, xsd__int pointNN, xsd__int segNN, xsd__int segLen, adb__queryResult &adbQueryResult);
