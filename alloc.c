#include "alloc.h"
#include "system.h"


void alloc_initialize (struct Alloc *alloc)
{
  int i;
  alloc->chunks = 0;
  for (i = 0; i < 32; i++)
    {
      alloc->buckets[i] = 0;
    }
  alloc->default_mmap_size = 1<<15;
}
void alloc_destroy (struct Alloc *alloc)
{
  struct AllocMmapChunk *tmp, *next;
  for (tmp = alloc->chunks; tmp != 0; tmp = next)
    {
      next = tmp->next;
      system_munmap (tmp->buffer, tmp->size);
    }
  alloc->chunks = 0;
}

static uint32_t round_to (uint32_t v, uint32_t to)
{
  return (v + (to - (v % to)));
}

static uint32_t chunk_overhead (void)
{
  return round_to (sizeof (struct AllocMmapChunk), 16);
}

static void alloc_chunk (struct Alloc *alloc, uint32_t size)
{
  size = round_to (size, 4096);
  uint8_t *map = system_mmap_anon (size);
  struct AllocMmapChunk *chunk = (struct AllocMmapChunk*) (map);
  chunk->buffer = map;
  chunk->size = size;
  chunk->brk = chunk_overhead ();
  chunk->next = alloc->chunks;
  alloc->chunks = chunk;
}

static uint8_t *alloc_brk (struct Alloc *alloc, uint32_t needed)
{
  struct AllocMmapChunk *tmp;
  for (tmp = alloc->chunks; tmp != 0; tmp = tmp->next)
    {
      if (tmp->size - tmp->brk >= needed)
	{
	  uint8_t *buffer = tmp->buffer + tmp->brk;
	  tmp->brk += needed;
	  return buffer;
	}
    }
  alloc_chunk (alloc, alloc->default_mmap_size);
  return alloc_brk (alloc, needed);
}
static uint8_t size_to_bucket (uint32_t sz)
{
  uint8_t bucket = 0;
  uint32_t size = sz;
  size--;
  while (size > 7)
    {
      size >>= 1;
      bucket++;
    }
  return bucket;
}
static uint32_t bucket_to_size (uint8_t bucket)
{
  uint32_t size = (1<<(bucket+3));
  return size;
}

uint8_t *alloc_malloc (struct Alloc *alloc, uint32_t size)
{
  if (size < (alloc->default_mmap_size - chunk_overhead ()))
    {
      uint8_t bucket = size_to_bucket (size);
      if (alloc->buckets[bucket] == 0)
	{
	  struct AllocAvailable *avail = (struct AllocAvailable *) 
	    alloc_brk (alloc, bucket_to_size (bucket));
	  avail->next = 0;
	  alloc->buckets[bucket] = avail;
	}
      // fast path.
      struct AllocAvailable *avail = alloc->buckets[bucket];
      alloc->buckets[bucket] = avail->next;
      return (uint8_t*)avail;
    }
  else
    {
      alloc_chunk (alloc, size + chunk_overhead ());
      return alloc_brk (alloc, size);
    }
}
void alloc_free (struct Alloc *alloc, uint8_t *buffer, uint32_t size)
{
  if (size < (alloc->default_mmap_size - chunk_overhead ()))
    {
      // return to bucket list.
      uint8_t bucket = size_to_bucket (size);
      struct AllocAvailable *avail = (struct AllocAvailable *)buffer;
      avail->next = alloc->buckets[bucket];
      alloc->buckets[bucket] = avail;
    }
  else
    {
      struct AllocMmapChunk *tmp, *prev;
      for (tmp = alloc->chunks, prev = 0; tmp != 0; prev = tmp, tmp = tmp->next)
	{
	  if (tmp->buffer == buffer && tmp->size == size)
	    {
	      if (prev == 0)
		{
		  alloc->chunks = tmp->next;
		}
	      else
		{
		  prev->next = tmp->next;
		}
	      system_munmap (tmp->buffer, tmp->size);
	      break;
	    }
	}
    }
}
