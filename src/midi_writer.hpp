#pragma once

#include <string>
#include <vector>

struct NoteEvent {
    int midiNote;
    double start;
    double end;
};

void writeMidiFile(const std::vector<NoteEvent>& events,
                   const std::string& path);
