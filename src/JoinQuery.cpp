#include "../include/JoinQuery.hpp"
#include <iostream>
#include <string>
#include <cassert>
#include <fstream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <charconv>
#include <unordered_map>

using namespace std;

class MMapFile{
   public:
   const char* begin;
   const char* end;
   size_t size;
   int handle = -1;
   MMapFile(const char *fileName);
   void close();

   private:
   void* data;

};
MMapFile::MMapFile(const char *fileName)
{
   handle = open(fileName, O_RDONLY);
   lseek(handle, 0, SEEK_END);
   size = lseek(handle, 0, SEEK_CUR);
   data = mmap(nullptr, size, PROT_READ, MAP_SHARED, handle, 0);
   begin = static_cast<const char*>(data);
   end = begin + size;
};
void MMapFile::close()
{
   munmap(data, size);
   ::close(handle);
}

static constexpr uint64_t buildPattern(char sep) {
   uint64_t v = sep;
   return v | (v << 8) | (v << 16) | (v << 24) | (v << 32) | (v << 40) | (v << 48) | (v << 56);
}

template <char sep>
static const char *findPattern(const char *iter, const char *end) {
   auto end8 = end - 8;
   constexpr uint64_t pattern = buildPattern(sep);
   for (; iter < end8; iter += 8) {
      uint64_t block = *reinterpret_cast<const uint64_t *>(iter);
      constexpr uint64_t high = 0x8080808080808080ull;
      constexpr uint64_t low = ~high;
      uint64_t lowChars = (~block) & high;
      uint64_t foundPattern = ~((((block & low) ^ pattern) + low) & high);
      uint64_t matches = foundPattern & lowChars;
      if (matches)
         return iter + (__builtin_ctzll(matches) >> 3) + 1;
   }

   while ((iter < end) && ((*iter) != sep))
      ++iter;
   if (iter < end)
      ++iter;
   return iter;
}

template <char sep>
static const char *findNthPattern(const char *iter, const char *end, unsigned n) {
   auto end8 = end - 8;
   constexpr uint64_t pattern = buildPattern(sep);
   for (; iter < end8; iter += 8) {
      uint64_t block = *reinterpret_cast<const uint64_t *>(iter);
      constexpr uint64_t high = 0x8080808080808080ull;
      constexpr uint64_t low = ~high;
      uint64_t lowChars = (~block) & high;
      uint64_t foundPattern = ~((((block & low) ^ pattern) + low) & high);
      uint64_t matches = foundPattern & lowChars;
      if (matches){
         unsigned hits = __builtin_popcountll(matches);
         if(hits >= n){
            for(;n>1; n--){
               matches &= matches-1;
            }
            return iter + (__builtin_ctzll(matches) >> 3) + 1;
         }
         n -= hits;
      }

   }

   for (;iter<end; iter++) {
      if ((*iter) == sep){
         if(--n == 0) return iter +1;
      }
   }
   return end;
}
unordered_map<string , string> c_dict;
unordered_map<string , string> o_dict;
unordered_map<string , pair<size_t , size_t>> l_dict;
//---------------------------------------------------------------------------
JoinQuery::JoinQuery(string lineitem, string order,
                     string customer)
{
   MMapFile c_file = MMapFile(customer.data());

   string c_key, c_mktsegment;
   for (auto begin = c_file.begin, end = c_file.end; begin < end;) {
      c_key  = ""; c_mktsegment = "";
      for (;begin < end;) {
         if (*begin == '|') break;
         c_key += (*begin++);
      }
      begin = findNthPattern<'|'>(begin+1, end, 5);
      for (;begin < end;) {
         if (*begin == '|') break;
         c_mktsegment += (*begin++);
      }
      begin = findPattern<'\n'>(begin+1, end);
      c_dict.insert({c_key, c_mktsegment});
   }
   c_file.close();

   MMapFile o_file = MMapFile(order.data());

   string o_key, o_custkey;
   for (auto begin = o_file.begin, end = o_file.end; begin < end;) {
      o_key  = ""; o_custkey = "";
      for (;begin < end;) {
         if (*begin == '|') break;
         o_key += (*begin++);
      }
      for (;begin < end;) {
         if (*(++begin) == '|') break;
         o_custkey += *begin;
      }
      begin = findPattern<'\n'>(begin+1, end);
      o_dict.insert({o_key, o_custkey});
   }
   o_file.close();

   MMapFile l_file = MMapFile(lineitem.data());

   string l_orderkey; string quantity;
   for (auto begin = l_file.begin, end = l_file.end; begin < end;) {
      l_orderkey  = ""; quantity = "";
      for (;begin < end;) {
         if (*begin == '|') break;
         l_orderkey += (*begin++);
      }
      begin = findNthPattern<'|'>(begin+1, end, 3);
      for (;begin < end;) {
         if (*begin == '|') break;
         quantity += (*begin++);
      }
      begin = findPattern<'\n'>(begin+1, end);
      if(l_dict.find(l_orderkey) == l_dict.end()){
         l_dict.insert({l_orderkey, pair<int, int>(atoi(quantity.data()),1)});
      } else{
         l_dict.find(l_orderkey)->second.first += atoi(quantity.data());
         l_dict.find(l_orderkey)->second.second += 1;
      }
   }
   l_file.close();


}
//---------------------------------------------------------------------------
size_t JoinQuery::avg(string segmentParam)
{
   size_t sum = 0; size_t nbrItems = 0;
   for (auto& it: l_dict) {
      auto order = o_dict.find(it.first);
      auto cust = c_dict.find(order->second);
      if (cust->second == segmentParam){
         sum += it.second.first;
         nbrItems += it.second.second;
      }
   }

   return uint64_t (sum*100)/nbrItems;
}


//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
size_t JoinQuery::lineCount(string rel)
{
   ifstream relation(rel);
   assert(relation);  // make sure the provided string references a file
   size_t n = 0;
   for (string line; getline(relation, line);) n++;
   return n;
}
//---------------------------------------------------------------------------
