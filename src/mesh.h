#ifndef SIMIT_MESH_H
#define SIMIT_MESH_H

#include <iostream>
#include <vector>
#include <array>
namespace simit{

///a triagular mesh data structure for loading
///plain text obj files. Does not work with quad mesh.
///Assumes one object per file.
///Only reads vertex and face and ignores all other attributes.
struct Mesh{
  ///vertex list
  std::vector<std::array< double,3> > v;
  ///triangle list
  std::vector<std::array< int,3> > t;

  ///return -1 if failed to load
  int load(const char * filename);
  ///return -1 if failed to load or format is unrecognized
  int load(std::istream & in);
  ///return -1 if failed to save
  int save(const char * filename);
  int save(std::ostream & out);
};

///a struct used to load custom volumetric mesh file.
///File format:
///#vertices xxx
///#elements xxx
///x1 y1 z1
///x2 y2 z2
///8 0 1 2 3 4 5 6 7
///8 4 5 6 7 8 9 10 11
struct MeshVol{
  ///vertex list
  std::vector<std::array< double,3> > v;
  ///element list
  std::vector<std::vector<int> > e;
  
  ///return -1 if failed to load
  int load(const char * filename);
  ///return -1 if failed to load or format is unrecognized
  int load(std::istream & in);
  ///return -1 if failed to save
  int save(const char * filename);
  int save(std::ostream & out);
};

}

#endif
 
