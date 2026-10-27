/*
 * mod-coa-playerbots
 *
 * Bot Dialogue System - Line Pool, JSON Loader, and Defaults.
 */

#ifndef COA_PLAYERBOTS_DIALOGUE_POOL_H
#define COA_PLAYERBOTS_DIALOGUE_POOL_H

#include "DialogueLine.h"
#include "DialogueTypes.h"
#include <unordered_map>
#include <vector>
#include <string>

class DialoguePool
{
public:
    DialoguePool();

    void Clear();
    bool AddLine(DialogueLine const& line, std::string& outError);
    std::vector<DialogueLine const*> GetLinesForBeat(DialogueBeat beat, bool isMysteryQuery = false) const;
    DialogueLine const* FindLineById(std::string const& id) const;

    size_t TotalLines() const;
    size_t BeatCount() const;

    void LoadDefaults();
    bool LoadFromJsonFile(std::string const& filePath, std::string& outError);
    bool LoadFromJsonString(std::string const& jsonStr, std::string& outError);
    bool LoadFromDirectory(std::string const& dirPath, std::string& outError);

private:
    std::unordered_map<DialogueBeat, std::vector<DialogueLine>> _pool;
    std::unordered_map<DialogueBeat, std::vector<DialogueLine>> _mysteryPool;
    std::unordered_map<std::string, DialogueLine> _linesById;

    static bool IsMysteryBeat(DialogueBeat beat);
};

#endif // COA_PLAYERBOTS_DIALOGUE_POOL_H
