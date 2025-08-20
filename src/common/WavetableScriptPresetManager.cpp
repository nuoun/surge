/*
 * Surge XT - a free and open source hybrid synthesizer,
 * built by Surge Synth Team
 *
 * Learn more at https://surge-synthesizer.github.io/
 *
 * Copyright 2018-2024, various authors, as described in the GitHub
 * transaction log.
 *
 * Surge XT is released under the GNU General Public Licence v3
 * or later (GPL-3.0-or-later). The license is found in the "LICENSE"
 * file in the root of this repository, or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 *
 * Surge was a commercial product from 2004-2018, copyright and ownership
 * held by Claes Johanson at Vember Audio during that period.
 * Claes made Surge open source in September 2018.
 *
 * All source for Surge XT is available at
 * https://github.com/surge-synthesizer/surge
 */

#include "WavetableScriptPresetManager.h"
#include <iostream>
#include "DebugHelpers.h"
#include "SurgeStorage.h"
#include "tinyxml/tinyxml.h"
#include "sst/plugininfra/strnatcmp.h"

namespace Surge
{
namespace Storage
{

const static std::string PresetDir = "Wavetable Script Presets";
const static std::string PresetXtn = ".wtspreset";

void WavetableScriptPreset::savePresetToUser(const fs::path &location, SurgeStorage *s, int scene,
                                             int oscid)
{
    try
    {
        auto osc = &(s->getPatch().scene[scene].osc[oscid]);
        std::string script = osc->wavetable_formula;

        auto containingPath = s->userDataPath / fs::path{PresetDir};
        fs::create_directories(containingPath);
        auto fullLocation =
            fs::path({containingPath / location}).replace_extension(fs::path{PresetXtn});

        TiXmlDeclaration decl("1.0", "UTF-8", "yes");

        TiXmlDocument doc;
        doc.InsertEndChild(decl);

        TiXmlElement wts("wts");

        TiXmlElement params("params");
        params.SetAttribute("name", osc->wavetable_display_name);
        params.SetAttribute("nframes", osc->wavetable_formula_nframes);
        params.SetAttribute("res_base", osc->wavetable_formula_res_base);
        wts.InsertEndChild(params);

        TiXmlElement fm("wavetable_formula");
        s->getPatch().wtsToXMLElement(&(s->getPatch().scene[scene].osc[oscid]), fm);
        wts.InsertEndChild(fm);

        doc.InsertEndChild(wts);

        if (!doc.SaveFile(fullLocation))
        {
            // uhh ... do something I guess?
            std::cout << "Could not save" << std::endl;
        }

        forcePresetRescan();
    }
    catch (const fs::filesystem_error &e)
    {
        s->reportError(e.what(), "Unable to save LFO Preset");
    }
}

/*
 * Given a completed path, load the preset into our storage
 */
void WavetableScriptPreset::loadPresetFrom(const fs::path &location, SurgeStorage *s, int scene,
                                           int oscid)
{
    auto osc = &(s->getPatch().scene[scene].osc[oscid]);

    TiXmlDocument doc;
    doc.LoadFile(location);
    auto wts = TINYXML_SAFE_TO_ELEMENT(doc.FirstChildElement("wts"));
    if (!wts)
    {
        std::cout << "Unable to find wts node in document" << std::endl;
        return;
    }

    auto params = TINYXML_SAFE_TO_ELEMENT(wts->FirstChildElement("params"));
    if (!params)
    {
        std::cout << "NO PARAMS" << std::endl;
        return;
    }

    const char *nameAttr = params->Attribute("name");
    if (nameAttr)
        osc->wavetable_display_name = nameAttr;

    int nframes = 0;
    if (params->QueryIntAttribute("nframes", &nframes) == TIXML_SUCCESS)
        osc->wavetable_formula_nframes = nframes;

    int res_base = 0;
    if (params->QueryIntAttribute("res_base", &res_base) == TIXML_SUCCESS)
        osc->wavetable_formula_res_base = res_base;

    auto frm = wts->FirstChildElement("wavetable_formula");
    if (frm)
        s->getPatch().wtsFromXMLElement(&(s->getPatch().scene[scene].osc[oscid]), frm);
}

/*
 * Note: Clients rely on this being sorted by category path if you change it
 */
std::vector<WavetableScriptPreset::Category> WavetableScriptPreset::getPresets(SurgeStorage *s,
                                                                               PresetScanMode mode)
{
    if (mode == PresetScanMode::UserOnly && haveScannedUser)
        return scannedUserPresets;
    if (mode == PresetScanMode::FactoryOnly && haveScannedFactory)
        return scannedFactoryPresets;

    auto factoryPath = s->datapath / fs::path{"wavetable_script_presets"};
    auto userPath = s->userDataPath / fs::path{PresetDir};

    std::map<std::string, Category> resMap; // handy it is sorted!

    // Check the mode to set what folder to scan
    std::vector<std::pair<fs::path, bool>> scanTargets;
    if (mode == PresetScanMode::FactoryOnly)
        scanTargets.emplace_back(factoryPath, false);
    if (mode == PresetScanMode::UserOnly)
        scanTargets.emplace_back(userPath, true);

    for (auto &[p, isU] : scanTargets)
    {
        try
        {
            // Category currentCategory;
            for (auto &d : fs::recursive_directory_iterator(p))
            {
                auto dp = fs::path(d);
                auto base = dp.stem();
                auto ext = dp.extension();
                if (path_to_string(ext) != PresetXtn)
                {
                    continue;
                }
                auto rd = path_to_string(dp.replace_filename(fs::path()));
                rd = rd.substr(path_to_string(p).length() + 1);
                rd = rd.substr(0, rd.length() - 1);

                auto catName = rd;
                auto ppos = rd.rfind(fs::path::preferred_separator);
                auto pd = std::string();
                if (ppos != std::string::npos)
                {
                    pd = rd.substr(0, ppos);
                    catName = rd.substr(ppos + 1);
                }
                if (resMap.find(rd) == resMap.end())
                {
                    resMap[rd] = Category();
                    resMap[rd].name = catName;
                    resMap[rd].parentPath = pd;
                    resMap[rd].path = rd;

                    /*
                     * We only create categories if we find a preset. So that means parent
                     * directories with just subdirs need categories made. This recurses up as far
                     * as we need to go
                     */
                    while (pd != "" && resMap.find(pd) == resMap.end())
                    {
                        auto cd = pd;
                        catName = cd;
                        ppos = cd.rfind(fs::path::preferred_separator);

                        if (ppos != std::string::npos)
                        {
                            pd = cd.substr(0, ppos);
                            catName = cd.substr(ppos + 1);
                        }
                        else
                        {
                            pd = "";
                        }

                        resMap[cd] = Category();
                        resMap[cd].name = catName;
                        resMap[cd].parentPath = pd;
                        resMap[cd].path = cd;
                    }
                }

                Preset prs;
                prs.name = path_to_string(base);
                prs.path = fs::path(d);
                resMap[rd].presets.push_back(prs);
            }
        }
        catch (const fs::filesystem_error &e)
        {
            // That's OK!
        }
    }

    std::vector<Category> res;
    for (auto &m : resMap)
    {
        std::sort(m.second.presets.begin(), m.second.presets.end(),
                  [](const Preset &a, const Preset &b) {
                      return strnatcasecmp(a.name.c_str(), b.name.c_str()) < 0;
                  });

        res.push_back(m.second);
    }

    if (mode == PresetScanMode::UserOnly)
    {
        scannedUserPresets = res;
        haveScannedUser = true;
    }
    if (mode == PresetScanMode::FactoryOnly)
    {
        scannedFactoryPresets = res;
        haveScannedFactory = true;
    }

    return res;
}

void WavetableScriptPreset::forcePresetRescan()
{
    haveScannedUser = false;
    scannedUserPresets.clear();

    haveScannedFactory = false;
    scannedFactoryPresets.clear();
}
} // namespace Storage
} // namespace Surge
