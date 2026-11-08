#pragma once
#include <deque>
#include <string>
#include <unordered_map>
#include <map>
#include <vector>
#include <fstream>
#include <iomanip>
#include <utility>

namespace erebus {

struct TelemetrySnapshot {
    double timeS{};
    std::map<std::string, double> channels;
};

// Circular time-series buffer keyed by string, plus dedicated recording/export session.
class TelemetryRecorder {
public:
    static constexpr std::size_t DefaultMax = 3600;

    explicit TelemetryRecorder(std::size_t maxPoints = DefaultMax)
        : maxPoints_(maxPoints) {}

    void record(const std::string& key, double timeS, double value) {
        auto& buf = data_[key];
        if (buf.size() >= maxPoints_) buf.pop_front();
        buf.emplace_back(timeS, value);
    }

    [[nodiscard]] bool has(const std::string& key) const {
        const auto it = data_.find(key);
        return it != data_.end() && !it->second.empty();
    }

    [[nodiscard]] const std::deque<std::pair<double,double>>& get(const std::string& key) const {
        static const std::deque<std::pair<double,double>> empty;
        const auto it = data_.find(key);
        return it != data_.end() ? it->second : empty;
    }

    void clear() {
        data_.clear();
        session_.clear();
        recording_ = false;
    }

    void clear(const std::string& key) { data_.erase(key); }

    // ── Dedicated recording session (Hit Record -> Hit Stop & Export) ─────────
    void startSession() {
        session_.clear();
        recording_ = true;
    }

    void stopSession() {
        recording_ = false;
    }

    [[nodiscard]] bool isRecording() const noexcept { return recording_; }
    [[nodiscard]] std::size_t sessionRecordCount() const noexcept { return session_.size(); }
    [[nodiscard]] const std::vector<TelemetrySnapshot>& sessionRecords() const noexcept { return session_; }

    void recordSessionSnapshot(double timeS, const std::map<std::string, double>& channels) {
        if (!recording_) return;
        session_.push_back({timeS, channels});
    }

    // ── CSV Exporter ─────────────────────────────────────────────────────────
    static bool exportRecordsToCsv(const std::string& filePath,
                                  const std::vector<TelemetrySnapshot>& records) {
        if (records.empty()) return false;
        std::ofstream out(filePath);
        if (!out.is_open()) return false;

        // Build channel key list from the first record or union of all
        std::map<std::string, bool> allKeys;
        for (const auto& rec : records) {
            for (const auto& [k, _] : rec.channels) allKeys[k] = true;
        }

        // CSV Header
        out << "time_seconds,time_days,time_years";
        for (const auto& [k, _] : allKeys) {
            out << "," << k;
        }
        out << "\n";

        // CSV Rows
        out << std::setprecision(10);
        for (const auto& rec : records) {
            const double t = rec.timeS;
            out << t << "," << (t / 86400.0) << "," << (t / (365.25 * 86400.0));
            for (const auto& [k, _] : allKeys) {
                out << ",";
                const auto it = rec.channels.find(k);
                if (it != rec.channels.end()) {
                    out << it->second;
                }
            }
            out << "\n";
        }
        return true;
    }

    // ── JSON Exporter ────────────────────────────────────────────────────────
    static bool exportRecordsToJson(const std::string& filePath,
                                   const std::vector<TelemetrySnapshot>& records,
                                   const std::string& presetName = "Default") {
        if (records.empty()) return false;
        std::ofstream out(filePath);
        if (!out.is_open()) return false;

        out << "{\n";
        out << "  \"project\": \"EREBUS\",\n";
        out << "  \"preset\": \"" << presetName << "\",\n";
        out << "  \"record_count\": " << records.size() << ",\n";
        out << "  \"records\": [\n";
        out << std::setprecision(10);

        for (std::size_t i = 0; i < records.size(); ++i) {
            const auto& rec = records[i];
            out << "    {\n";
            out << "      \"time_s\": " << rec.timeS << ",\n";
            out << "      \"time_days\": " << (rec.timeS / 86400.0) << ",\n";
            out << "      \"time_years\": " << (rec.timeS / (365.25 * 86400.0)) << ",\n";
            out << "      \"channels\": {";
            bool first = true;
            for (const auto& [k, v] : rec.channels) {
                if (!first) out << ", ";
                out << "\"" << k << "\": " << v;
                first = false;
            }
            out << "}\n";
            out << "    }" << (i + 1 < records.size() ? "," : "") << "\n";
        }
        out << "  ]\n";
        out << "}\n";
        return true;
    }

    // Generate snapshot list from live circular buffer
    [[nodiscard]] std::vector<TelemetrySnapshot> liveBufferSnapshots() const {
        if (data_.empty()) return {};

        // Find common timestamps from energy.total or any available key
        std::map<double, std::map<std::string, double>> rows;
        for (const auto& [k, buf] : data_) {
            for (const auto& [t, v] : buf) {
                rows[t][k] = v;
            }
        }

        std::vector<TelemetrySnapshot> result;
        result.reserve(rows.size());
        for (auto& [t, channels] : rows) {
            result.push_back({t, std::move(channels)});
        }
        return result;
    }

private:
    std::size_t maxPoints_;
    std::unordered_map<std::string, std::deque<std::pair<double,double>>> data_;
    std::vector<TelemetrySnapshot> session_;
    bool recording_{false};
};

} // namespace erebus
