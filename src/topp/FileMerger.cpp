// --------------------------------------------------------------------------
//                   OpenMS -- Open-Source Mass Spectrometry
// --------------------------------------------------------------------------
// Copyright The OpenMS Team -- Eberhard Karls University Tuebingen,
// ETH Zurich, and Freie Universitaet Berlin 2002-2015.
//
// This software is released under a three-clause BSD license:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of any author or any participating institution
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
// For a full list of authors, refer to the file AUTHORS.
// --------------------------------------------------------------------------
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL ANY OF THE AUTHORS OR THE CONTRIBUTING
// INSTITUTIONS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// --------------------------------------------------------------------------
// $Maintainer: Chris Bielow $
// $Authors: Marc Sturm, Chris Bielow, Hendrik Weisser $
// --------------------------------------------------------------------------

#include <OpenMS/FORMAT/TextFile.h>
#include <OpenMS/FORMAT/FileHandler.h>
#include <OpenMS/FORMAT/FileTypes.h>
#include <OpenMS/FORMAT/MzMLFile.h>
#include <OpenMS/FORMAT/TraMLFile.h>
#include <OpenMS/FORMAT/FeatureXMLFile.h>
#include <OpenMS/FORMAT/ConsensusXMLFile.h>
#include <OpenMS/DATASTRUCTURES/StringListUtils.h>

#include <OpenMS/APPLICATIONS/TOPPBase.h>

#include <boost/regex.hpp>

using namespace OpenMS;
using namespace std;

//-------------------------------------------------------------
//Doxygen docu
//-------------------------------------------------------------

/**
  @page TOPP_FileMerger FileMerger

  @brief Merges several files. Multiple output format supported, depending on input format.

  <center>
  <table>
  <tr>
  <td ALIGN = "center" BGCOLOR="#EBEBEB"> pot. predecessor tools </td>
  <td VALIGN="middle" ROWSPAN=2> \f$ \longrightarrow \f$ FileMerger \f$ \longrightarrow \f$</td>
  <td ALIGN = "center" BGCOLOR="#EBEBEB"> pot. successor tools </td>
  </tr>
  <tr>
  <td VALIGN="middle" ALIGN = "center" ROWSPAN=1> any tool/instrument producing merge able files </td>
  <td VALIGN="middle" ALIGN = "center" ROWSPAN=1> any tool operating merged files (e.g. @ref TOPP_XTandemAdapter) </td>
  </tr>
  </table>
  </center>

  The meta information that is valid for the whole experiment (e.g. MS instrument and sample)
  is taken from the first file.

  The retention times for the individual scans are taken from either:
  <ul>
  <li>the input file meta data (e.g. mzML)
  <li>from the input file names (name must contain 'rt' directly followed by a number, e.g. 'myscan_rt3892.98_MS2.dta')
  <li>as a list (one RT for each file)
  <li>or are auto-generated (starting at 1 with 1 second increment).
  </ul>

  <B>The command line parameters of this tool are:</B>
  @verbinclude TOPP_FileMerger.cli
  <B>INI file documentation of this tool:</B>
  @htmlinclude TOPP_FileMerger.html
 */

// We do not want this class to show up in the docu:
/// @cond TOPPCLASSES

class TOPPFileMerger :
  public TOPPBase
{
public:

  TOPPFileMerger() :
    TOPPBase("FileMerger", "Merges several MS files into one file.")
  {
  }

protected:

  void registerOptionsAndFlags_()
  {
    StringList valid_in = ListUtils::create<String>("mzData,mzXML,mzML,dta,dta2d,mgf,featureXML,consensusXML,fid,traML");
    registerInputFileList_("in", "<files>", StringList(), "Input files separated by blank");
    setValidFormats_("in", valid_in);
    registerStringOption_("in_type", "<type>", "", "Input file type (default: determined from file extension or content)", false);
    setValidStrings_("in_type", valid_in);
    registerOutputFile_("out", "<file>", "", "Output file");
    setValidFormats_("out", ListUtils::create<String>("mzML,featureXML,consensusXML,traML"));

    registerFlag_("annotate_file_origin", "Store the original filename in each feature using meta value \"file_origin\" (for featureXML and consensusXML only).");

    addEmptyLine_();
    registerTOPPSubsection_("raw", "Flags for non-featureXML input/output");
    registerFlag_("raw:rt_auto", "Assign retention times automatically (integers starting at 1)");
    registerDoubleList_("raw:rt_custom", "<rt>", DoubleList(), "List of custom retention times that are assigned to the files. The number of given retention times must be equal to the number of given input file.", false);
    registerFlag_("raw:rt_filename", "If this flag is set FileMerger tries to guess the rt of the file name.\n"
                                     "This option is useful for merging DTA file, which should contain the string\n"
                                     "'rt' directly followed by a floating point number:\n"
                                     "i.e. my_spectrum_rt2795.15.dta");
    registerIntOption_("raw:ms_level", "<num>", 2, "This option is useful for use with DTA files which does not contain MS level information. The given level is assigned to the spectra.", false);
    registerFlag_("raw:user_ms_level", "If this flag is set, the MS level given above is used");
  }

  ExitCodes main_(int, const char**)
  {

    //-------------------------------------------------------------
    // parameter handling
    //-------------------------------------------------------------
    //file list
    StringList file_list = getStringList_("in");

    //file type
    FileHandler file_handler;
    FileTypes::Type force_type;
    if (getStringOption_("in_type").size() > 0)
    {
      force_type = FileTypes::nameToType(getStringOption_("in_type"));
    }
    else
    {
      force_type = file_handler.getType(file_list[0]);
    }

    //output file names and types
    String out_file = getStringOption_("out");

    //-------------------------------------------------------------
    // calculations
    //-------------------------------------------------------------

    bool annotate_file_origin =  getFlag_("annotate_file_origin");

    if (force_type == FileTypes::FEATUREXML)
    {
      FeatureMap out;
      for (Size i = 0; i < file_list.size(); ++i)
      {
        FeatureMap map;
        FeatureXMLFile fh;
        fh.load(file_list[i], map);

        if (annotate_file_origin)
        {
          for (FeatureMap::iterator it = map.begin(); it != map.end(); ++it)
          {
            it->setMetaValue("file_origin", DataValue(file_list[i]));
          }
        }
        out += map;
      }

      //-------------------------------------------------------------
      // writing output
      //-------------------------------------------------------------

      //annotate output with data processing info
      addDataProcessing_(out, getProcessingInfo_(DataProcessing::FORMAT_CONVERSION));

      FeatureXMLFile f;
      f.store(out_file, out);

    }
    else if (force_type == FileTypes::CONSENSUSXML)
    {
      ConsensusMap out;
      ConsensusXMLFile fh;
      fh.load(file_list[0], out);
      //skip first file
      for (Size i = 1; i < file_list.size(); ++i)
      {
        ConsensusMap map;
        ConsensusXMLFile fh;
        fh.load(file_list[i], map);

        if (annotate_file_origin)
        {
          for (ConsensusMap::iterator it = map.begin(); it != map.end(); ++it)
          {
            it->setMetaValue("file_origin", DataValue(file_list[i]));
          }
        }
        out += map;
      }

      //-------------------------------------------------------------
      // writing output
      //-------------------------------------------------------------

      //annotate output with data processing info
      addDataProcessing_(out, getProcessingInfo_(DataProcessing::FORMAT_CONVERSION));

      ConsensusXMLFile f;
      f.store(out_file, out);
    }
    else if (force_type == FileTypes::TRAML)
    {
      TargetedExperiment out;
      for (Size i = 0; i < file_list.size(); ++i)
      {
        TargetedExperiment map;
        TraMLFile fh;
        fh.load(file_list[i], map);
        out += map;
      }

      //-------------------------------------------------------------
      // writing output
      //-------------------------------------------------------------

      //annotate output with data processing info
      Software software;
      software.setName("FileMerger");
      software.setVersion(VersionInfo::getVersion());
      out.addSoftware(software);

      TraMLFile f;
      f.store(out_file, out);
    }
    else
    {
      // we might want to combine different types, thus we only
      // query in_type (which applies to all files)
      // and not the suffix or content of a single file
      //... well, what was coded here did a) nothing what was not done already in the code and b) nothing what the above comment kind of claims
      // instead, type assesment for loading has to be done for each file in the list

      //rt
      bool rt_auto_number = getFlag_("raw:rt_auto");
      bool rt_filename = getFlag_("raw:rt_filename");
      bool rt_custom = false;
      DoubleList custom_rts = getDoubleList_("raw:rt_custom");
      if (custom_rts.size() != 0)
      {
        rt_custom = true;
        if (custom_rts.size() != file_list.size())
        {
          writeLog_("Custom retention time list must have as many elements as there are input files!");
          printUsage_();
          return ILLEGAL_PARAMETERS;
        }
      }

      //ms level
      bool user_ms_level = getFlag_("raw:user_ms_level");

      MSExperiment<> out;
      out.reserve(file_list.size());
      UInt rt_auto = 0;
      UInt native_id = 0;
      std::vector<MSChromatogram<ChromatogramPeak> > all_chromatograms;
      for (Size i = 0; i < file_list.size(); ++i)
      {
        String filename = file_list[i];

        //load file
        force_type = file_handler.getType(file_list[i]);
        MSExperiment<> in;
        file_handler.loadExperiment(filename, in, force_type, log_type_);

        if (in.empty() && in.getChromatograms().empty())
        {
          writeLog_(String("Warning: Empty file '") + filename + "'!");
          continue;
        }
        out.reserve(out.size() + in.size());

        //warn if custom RT and more than one scan in input file
        if (rt_custom && in.size() > 1)
        {
          writeLog_(String("Warning: More than one scan in file '") + filename + "'! All scans will have the same retention time!");
        }

        for (MSExperiment<>::const_iterator it2 = in.begin(); it2 != in.end(); ++it2)
        {
          //handle rt
          float rt_final = it2->getRT();
          if (rt_auto_number)
          {
            rt_final = ++rt_auto;
          }
          else if (rt_custom)
          {
            rt_final = custom_rts[i];
          }
          else if (rt_filename)
          {
            static const boost::regex re("rt(\\d+(\\.\\d+)?)");
            boost::smatch match;
            bool found = boost::regex_search(filename, match, re);
            if (found)
            {
              rt_final = String(match[1]).toFloat();
            }
            else
            {
              writeLog_("Warning: could not extract retention time from filename '" + filename + "'");
            }
          }

          // none of the rt methods were successful
          if (rt_final < 0)
          {
            writeLog_(String("Warning: No valid retention time for output scan '") + rt_auto + "' from file '" + filename + "'");
          }

          out.addSpectrum(*it2);
          out.getSpectra().back().setRT(rt_final);
          out.getSpectra().back().setNativeID(native_id);

          if (user_ms_level)
          {
            out.getSpectra().back().setMSLevel((int)getIntOption_("raw:ms_level"));
          }
          ++native_id;
        }

        // if we had only one spectrum, we can annotate it directly, for more spectra, we just name the source file leaving the spectra unannotated (to avoid a long and redundant list of sourceFiles)
        if (in.size() == 1)
        {
          out.getSpectra().back().setSourceFile(in.getSourceFiles()[0]);
          in.getSourceFiles().clear(); // delete source file annotated from source file (its in the spectrum anyways)
        }
        // copy experimental settings from first file
        if (i == 0)
        {
          out.ExperimentalSettings::operator=(in);
        }
        else // otherwise append
        {
          out.getSourceFiles().insert(out.getSourceFiles().end(), in.getSourceFiles().begin(), in.getSourceFiles().end()); // could be emtpty if spectrum was annotated above, but that's ok then
        }

        // also add the chromatograms
        for (std::vector<MSChromatogram<ChromatogramPeak> >::const_iterator it2 = in.getChromatograms().begin(); it2 != in.getChromatograms().end(); ++it2)
        {
          all_chromatograms.push_back(*it2);
        }

      }
      // set the chromatograms
      out.setChromatograms(all_chromatograms);

      //-------------------------------------------------------------
      // writing output
      //-------------------------------------------------------------

      //annotate output with data processing info
      addDataProcessing_(out, getProcessingInfo_(DataProcessing::FORMAT_CONVERSION));

      MzMLFile f;
      f.setLogType(log_type_);
      f.store(out_file, out);

    }

    return EXECUTION_OK;
  }

};

int main(int argc, const char** argv)
{
  TOPPFileMerger tool;
  return tool.main(argc, argv);
}

/// @endcond
