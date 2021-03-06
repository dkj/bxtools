#include "bxsplit.h"

#include <string>
#include <getopt.h>
#include <iostream>
#include <sstream>

#include "SeqLib/BamReader.h"
#include "SeqLib/BamWriter.h"

struct BXTag {

  SeqLib::BamWriter w;
  size_t count = 0;
  SeqLib::BamRecordVector buff;
};

namespace opt {

  static std::string bam; // the bam to split
  static std::string analysis_id = "foo"; // unique prefix for output
  static bool verbose = false; 
  static bool noop = false; // dont write bams, just count
  static int min = 0; // minimum number of reads before writing
}

static const char* shortopts = "hvxb:a:m:";
static const struct option longopts[] = {
  { "help",                    no_argument, NULL, 'h' },
  { "no-output",               no_argument, NULL, 'x' },
  { "analysis-id",             required_argument, NULL, 'a' },
  { "verbose",                 no_argument, NULL, 'v' },
  { "min-reads",               required_argument, NULL, 'm' },
  { NULL, 0, NULL, 0 }
};

static const char *SPLIT_USAGE_MESSAGE =
"Usage: bxtools split <BAM/SAM/CRAM> -a <id> > bxcounts.tsv\n"
"Description: Split / count a BAM into multiple BAMs, one BAM per unique BX tag\n"
"\n"
"  General options\n"
"  -v, --verbose                        Select verbosity level (0-4). Default: 0 \n"
"  -h, --help                           Display this help and exit\n"
"  -a, --analysis-id                    ID to prefix output files with [foo]\n"
"  -x, --no-output                      Don't output BAMs (count only) [off]\n"
"  -m, --min-reads                      Minumum reads of given tag to see before writing [0]\n"
"\n";

void parseSplitOptions(int argc, char** argv) {

  bool die = false;

  if (argc < 2) 
    die = true;

  opt::bam = std::string(argv[1]);

  bool help = false;
  std::stringstream ss;

  for (char c; (c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1;) {
    std::istringstream arg(optarg != NULL ? optarg : "");
    switch (c) {
    case 'a': arg >> opt::analysis_id; break;
    case 'v': opt::verbose = true; break;
    case 'x': opt::noop = true; break;
    case 'm': arg >> opt::min; break;
    }
  }

  if (die || help) {
    std::cerr << "\n" << SPLIT_USAGE_MESSAGE;
    die ? exit(EXIT_FAILURE) : exit(EXIT_SUCCESS);
  }
}

void runSplit(int argc, char** argv) {
  
  parseSplitOptions(argc, argv);
  
  // opeen the BAM
  SeqLib::BamReader reader;
  if (!reader.Open(opt::bam)) {
    std::cerr << "Failed to open bam: " << opt::bam << std::endl;
    exit(EXIT_FAILURE);
  }
  
  // make a collection of writers
  std::unordered_map<std::string, BXTag> tags;

  // loop and write
  SeqLib::BamRecord r;
  size_t count = 0;
  while (reader.GetNextRecord(r)) {

    ++count;

    // sanity check
    if (count == 100000 && !tags.size())
      std::cerr << "****1e5 reads in and haven't hit BX tag yet****" << std::endl;

    std::string bx = r.GetZTag("BX");
    if (bx.empty()) {
      //std::cerr << "BX tag empty for read: " << r << std::endl;
      continue;
    }
      
    if (count % 1000000 == 0)
      std::cerr << "...at read " << SeqLib::AddCommas(count) << " at pos " << r.Brief() << std::endl;

    ++tags[bx].count;

    if (opt::noop)
      continue;
    
    if (tags[bx].count < opt::min) {
      tags[bx].buff.push_back(r);
      continue;
    }
    
    // have a buffer to clear or hit first read with no min
    if (tags[bx].buff.size() || (opt::min <= 0 && tags[bx].count == 1)) {   

      // need to establish a new writer?
      std::string bname = opt::analysis_id + "." + bx + ".bam";
      if (!tags[bx].w.Open(bname)) {
	std::cerr << "Could not open BAM: " << bname << std::endl;
	exit(EXIT_FAILURE);
      }
      
      std::cerr << "creating new output BAM: " << bname << std::endl;
      tags[bx].w.SetHeader(reader.Header());
      tags[bx].w.WriteHeader();
      for (const auto& rr : tags[bx].buff)
	tags[bx].w.WriteRecord(rr);
      tags[bx].buff.clear();
      continue;
    }
    
    if (!tags[bx].w.WriteRecord(r)) {
      std::cerr << "failed to write read " << r << " to BAM for " << bx << std::endl;
      exit(EXIT_FAILURE);
    }
    
  }

  // print the final counts to std::out
  for (const auto& b : tags)
    std::cout << b.first << "\t" << b.second.count << std::endl;
  
}
