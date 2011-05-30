#ifndef DE_OSM3S__BACKEND__RANDOM_FILE
#define DE_OSM3S__BACKEND__RANDOM_FILE

#include "random_file_index.h"
#include "types.h"

#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <vector>

using namespace std;

template< class TVal >
struct Random_File
{
private:
  Random_File(const Random_File& f) {}
  
public:
  typedef uint32 size_t;
  
  Random_File(Random_File_Index*);
  ~Random_File();
  
  TVal get(size_t pos);
  void put(size_t pos, const TVal& index);
  
private:
  bool changed;
  uint32 index_size;
  
  Raw_File val_file;
  Random_File_Index* index;
  Void_Pointer< uint8 > cache;
  size_t cache_pos, block_size;
  
  void move_cache_window(size_t pos);
};

/** Implementation Random_File: ---------------------------------------------*/

template< class TVal >
Random_File< TVal >::Random_File(Random_File_Index* index_)
  : changed(false), index_size(TVal::max_size_of()),
  val_file(index_->get_map_file_name(),
	   index_->writeable() ? O_RDWR|O_CREAT : O_RDONLY,
	   S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH, "Random_File:3"),
  index(index_),
  cache(index_->get_block_size()), cache_pos(index->npos),
  block_size(index_->get_block_size())
{}

template< class TVal >
Random_File< TVal >::~Random_File()
{
  move_cache_window(index->npos);  
  //delete index;
}

template< class TVal >
TVal Random_File< TVal >::get(size_t pos)
{
  move_cache_window(pos / (block_size/index_size));  
  return TVal(cache.ptr + (pos % (block_size/index_size))*index_size);
}

template< class TVal >
void Random_File< TVal >::put(size_t pos, const TVal& val)
{
  if (!index->writeable())
    throw File_Error(0, index->get_map_file_name(), "Random_File:2");
  
  move_cache_window(pos / (block_size/index_size));
  val.to_data(cache.ptr + (pos % (block_size/index_size))*index_size);
  changed = true;
}

template< class TVal >
void Random_File< TVal >::move_cache_window(size_t pos)
{
  // The cache already contains the needed position.
  if ((pos == cache_pos) && (cache_pos != index->npos))
    return;

  if (changed)
  {
    // Find an empty position.
    uint32 disk_pos;
    if (index->void_blocks.empty())
    {
      disk_pos = index->block_count;
      ++(index->block_count);
    }
    else
    {
      disk_pos = index->void_blocks.back();
      index->void_blocks.pop_back();
    }
    
    // Save the found position to the index.
    if (index->blocks.size() <= cache_pos)
      index->blocks.resize(cache_pos+1, index->npos);
    index->blocks[cache_pos] = disk_pos;
    
    // Write the data at the found position.
    lseek64(val_file.fd(), (int64)disk_pos*block_size, SEEK_SET);
    uint32 foo(write(val_file.fd(), cache.ptr, block_size)); foo = 0;
  }
  changed = false;
  
  if (pos == index->npos)
    return;
  
  if ((index->blocks.size() <= pos) || (index->blocks[pos] == index->npos))
  {
    // Reset the whole cache to zero.
    for (uint32 i = 0; i < block_size; ++i)
      *(cache.ptr + i) = 0;
  }
  else
  {
    lseek64(val_file.fd(), (int64)(index->blocks[pos])*block_size, SEEK_SET);
    uint32 foo(read(val_file.fd(), cache.ptr, block_size)); foo = 0;
  }
  cache_pos = pos;
}

#endif