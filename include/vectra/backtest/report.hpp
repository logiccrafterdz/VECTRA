// Markdown replay report writer.
#pragma once

#include <iosfwd>
#include <string>

#include "vectra/backtest/replay_engine.hpp"

namespace vectra::backtest {

// Writes a full markdown report for a completed replay run, including
// explicit limitations and assumptions. Never presents synthetic data as
// evidence of a trading edge.
void write_markdown_report(std::ostream& out, const ReplayEngine& engine,
                           const std::string& source_name);

}  // namespace vectra::backtest
