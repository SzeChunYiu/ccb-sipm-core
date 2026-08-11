#include "ccb/sipm/Config.hh"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>

namespace ccb::sipm {

namespace {

void RequireProbability(double value, const char* name, bool allow_one = true) {
  const bool valid = std::isfinite(value) && value >= 0.0 &&
                     (allow_one ? value <= 1.0 : value < 1.0);
  if (!valid) {
    throw std::invalid_argument(std::string(name) +
                                " is outside its probability domain");
  }
}

void EmitJsonString(std::ostream& os, const char* key,
                    const std::string& value) {
  os << "    \"" << key << "\": \"";
  for (char c : value) {
    switch (c) {
      case '\\': os << "\\\\"; break;
      case '\"': os << "\\\""; break;
      case '\n': os << "\\n"; break;
      case '\r': os << "\\r"; break;
      case '\t': os << "\\t"; break;
      default: os << c;
    }
  }
  os << "\"";
}

double ParseDoubleEnv(const char* name, bool& ok) {
  const char* raw = std::getenv(name);
  if (raw == nullptr || raw[0] == '\0') {
    ok = false;
    return 0.0;
  }
  try {
    std::size_t consumed = 0;
    const double value = std::stod(std::string(raw), &consumed);
    ok = consumed == std::string(raw).size() && std::isfinite(value);
    return value;
  } catch (...) {
    ok = false;
    return 0.0;
  }
}

int ParseIntEnv(const char* name, bool& ok) {
  const char* raw = std::getenv(name);
  if (raw == nullptr || raw[0] == '\0') {
    ok = false;
    return 0;
  }
  try {
    std::size_t consumed = 0;
    const int value = std::stoi(std::string(raw), &consumed);
    ok = consumed == std::string(raw).size();
    return value;
  } catch (...) {
    ok = false;
    return 0;
  }
}

}  // namespace

void ModelConfig::validate() const {
  if (cells_x <= 0 || cells_y <= 0) {
    throw std::invalid_argument("cell dimensions must be positive");
  }
  if (!(active_width_mm > 0.0) || !(active_height_mm > 0.0)) {
    throw std::invalid_argument("active dimensions must be positive");
  }
  if (pde_curve.size() < 2) {
    throw std::invalid_argument("PDE curve requires at least two points");
  }
  for (std::size_t i = 0; i < pde_curve.size(); ++i) {
    if (!std::isfinite(pde_curve[i].wavelength_nm) ||
        !std::isfinite(pde_curve[i].pde)) {
      throw std::invalid_argument("PDE curve contains non-finite values");
    }
    RequireProbability(pde_curve[i].pde, "PDE value");
    if (i > 0 &&
        !(pde_curve[i].wavelength_nm > pde_curve[i - 1].wavelength_nm)) {
      throw std::invalid_argument(
          "PDE wavelengths must be strictly increasing");
    }
  }
  if (!std::isfinite(pde_scale) || pde_scale < 0.0) {
    throw std::invalid_argument("PDE scale must be finite and non-negative");
  }
  RequireProbability(coupling_efficiency, "coupling efficiency");
  if (!(recovery_time_ns > 0.0) || dead_time_ns < 0.0) {
    throw std::invalid_argument("invalid recovery/dead time");
  }
  if (!(gain_mean_pe > 0.0) || gain_sigma_fraction < 0.0 ||
      sptr_sigma_ns < 0.0) {
    throw std::invalid_argument("invalid gain or SPTR parameters");
  }
  if (dark_count_rate_hz < 0.0) {
    throw std::invalid_argument("dark count rate must be non-negative");
  }
  RequireProbability(prompt_crosstalk_probability,
                     "prompt crosstalk probability", false);
  RequireProbability(delayed_crosstalk_probability,
                     "delayed crosstalk probability", false);
  RequireProbability(afterpulse_fast_probability,
                     "fast afterpulse probability", false);
  RequireProbability(afterpulse_slow_probability,
                     "slow afterpulse probability", false);
  if (prompt_crosstalk_jitter_ns < 0.0 ||
      !(delayed_crosstalk_tau_ns > 0.0) ||
      !(afterpulse_fast_tau_ns > 0.0) ||
      !(afterpulse_slow_tau_ns > 0.0)) {
    throw std::invalid_argument("invalid correlated-noise time parameter");
  }
  if (crosstalk_neighbourhood != 4 && crosstalk_neighbourhood != 8) {
    throw std::invalid_argument("crosstalk neighbourhood must be 4 or 8");
  }
  if (!(window_end_ns > window_start_ns)) {
    throw std::invalid_argument("waveform window is empty");
  }
  if (!(history_start_ns <= window_start_ns)) {
    throw std::invalid_argument("history_start_ns must be <= window_start_ns");
  }
  if (!(sample_dt_ns > 0.0) || !(pulse_rise_ns > 0.0) ||
      !(pulse_decay_ns > pulse_rise_ns)) {
    throw std::invalid_argument("invalid waveform sampling or pulse constants");
  }
  if (shaper_integrator_stages < 1 || shaper_integrator_stages > 6) {
    throw std::invalid_argument("shaper_integrator_stages must be in [1,6]");
  }
  if (shaper_integrator_stages >= 2 &&
      !(shaper_extra_stage_tau_ns > 0.0)) {
    throw std::invalid_argument(
        "shaper_extra_stage_tau_ns must be positive");
  }

  const bool has_measured =
      !measured_impulse_t_ns.empty() || !measured_impulse_amplitude.empty();
  if (has_measured) {
    if (measured_impulse_t_ns.size() != measured_impulse_amplitude.size()) {
      throw std::invalid_argument(
          "measured_impulse_t_ns / measured_impulse_amplitude length mismatch");
    }
    if (measured_impulse_t_ns.size() < 2) {
      throw std::invalid_argument(
          "measured impulse needs at least two samples");
    }
    double max_abs = 0.0;
    double integral = 0.0;
    for (std::size_t i = 0; i < measured_impulse_t_ns.size(); ++i) {
      if (!std::isfinite(measured_impulse_t_ns[i]) ||
          !std::isfinite(measured_impulse_amplitude[i])) {
        throw std::invalid_argument(
            "measured impulse contains non-finite values");
      }
      if (i > 0 &&
          !(measured_impulse_t_ns[i] > measured_impulse_t_ns[i - 1])) {
        throw std::invalid_argument(
            "measured impulse times must be strictly increasing");
      }
      max_abs = std::max(max_abs, std::abs(measured_impulse_amplitude[i]));
      if (i > 0) {
        integral += 0.5 *
                    (measured_impulse_amplitude[i] +
                     measured_impulse_amplitude[i - 1]) *
                    (measured_impulse_t_ns[i] -
                     measured_impulse_t_ns[i - 1]);
      }
    }
    if (!(max_abs > 0.0)) {
      throw std::invalid_argument(
          "measured impulse is degenerate (zero peak); refusing ideal-delta "
          "fallback");
    }
    if (!(integral > 0.0)) {
      throw std::invalid_argument(
          "measured impulse has non-positive integral");
    }

    const std::size_t output_samples = static_cast<std::size_t>(
        std::floor((window_end_ns - window_start_ns) / sample_dt_ns)) + 1U;
    const double prehistory_span_ns =
        std::max(0.0, window_start_ns - history_start_ns);
    const std::size_t prehistory_samples = static_cast<std::size_t>(
        std::ceil(prehistory_span_ns / sample_dt_ns));
    const std::size_t kernel_samples = output_samples + prehistory_samples;
    const double grid_end =
        static_cast<double>(kernel_samples - 1U) * sample_dt_ns;
    const double support_lo = measured_impulse_t_ns.front();
    const double support_hi = measured_impulse_t_ns.back();
    if (!(support_hi >= 0.0 && support_lo <= grid_end)) {
      throw std::invalid_argument(
          "measured impulse time support does not overlap the history-complete "
          "runtime kernel grid");
    }
    if (impulse_model == "IDEAL_DELTA_TEST_ONLY") {
      throw std::invalid_argument(
          "IDEAL_DELTA_TEST_ONLY is incompatible with measured impulse data");
    }
    impulse_model = "MEASURED";
  } else if (impulse_model == "IDEAL_DELTA_TEST_ONLY") {
    if (!authorising) {
      throw std::invalid_argument(
          "IDEAL_DELTA_TEST_ONLY requires authorising=true");
    }
  } else {
    impulse_model = "GENERIC_CRRC";
  }

  if (impulse_model != "GENERIC_CRRC" && impulse_model != "MEASURED" &&
      impulse_model != "IDEAL_DELTA_TEST_ONLY") {
    throw std::invalid_argument("unknown impulse_model: " + impulse_model);
  }
  if (electronics_noise_sigma_pe < 0.0 || !(adc_lsb_pe > 0.0) ||
      adc_bits <= 0 || adc_bits > 30) {
    throw std::invalid_argument("invalid electronics/ADC parameters");
  }
  if (max_candidates == 0) {
    throw std::invalid_argument("max_candidates must be positive");
  }
}

int ModelConfig::ApplyEnvironmentOverrides(ModelConfig& c) {
  int applied = 0;
  bool ok = false;
  double d = 0.0;
  int i = 0;

  d = ParseDoubleEnv("CCB_SIPM_WINDOW_START_NS", ok);
  if (ok) { c.window_start_ns = d; ++applied; }
  d = ParseDoubleEnv("CCB_SIPM_WINDOW_END_NS", ok);
  if (ok) { c.window_end_ns = d; ++applied; }
  d = ParseDoubleEnv("CCB_SIPM_HISTORY_START_NS", ok);
  if (ok) { c.history_start_ns = d; ++applied; }
  d = ParseDoubleEnv("CCB_SIPM_SAMPLE_DT_NS", ok);
  if (ok) { c.sample_dt_ns = d; ++applied; }
  i = ParseIntEnv("CCB_SIPM_SHAPER_STAGES", ok);
  if (ok) { c.shaper_integrator_stages = i; ++applied; }
  d = ParseDoubleEnv("CCB_SIPM_SHAPER_TAU_NS", ok);
  if (ok) { c.pulse_decay_ns = d; ++applied; }
  d = ParseDoubleEnv("CCB_SIPM_SHAPER_EXTRA_TAU_NS", ok);
  if (ok) { c.shaper_extra_stage_tau_ns = d; ++applied; }
  i = ParseIntEnv("CCB_SIPM_ADC_BITS", ok);
  if (ok) { c.adc_bits = i; ++applied; }
  d = ParseDoubleEnv("CCB_SIPM_ADC_LSB_PE", ok);
  if (ok) { c.adc_lsb_pe = d; ++applied; }
  d = ParseDoubleEnv("CCB_SIPM_BASELINE_ADC", ok);
  if (ok) { c.baseline_adc = d; ++applied; }
  d = ParseDoubleEnv("CCB_SIPM_PDE_SCALE", ok);
  if (ok) { c.pde_scale = d; ++applied; }
  d = ParseDoubleEnv("CCB_SIPM_OVERVOLTAGE_V", ok);
  if (ok) { c.device_provenance.overvoltage_V = d; ++applied; }
  d = ParseDoubleEnv("CCB_SIPM_TEMPERATURE_C", ok);
  if (ok) { c.device_provenance.temperature_C = d; ++applied; }
  return applied;
}

ModelConfig ModelConfig::RepresentativeS13360_3050CS() {
  ModelConfig c;
  c.pde_curve = {
      {300.0, 0.06}, {350.0, 0.18}, {400.0, 0.33}, {450.0, 0.40},
      {476.0, 0.38}, {500.0, 0.35}, {550.0, 0.27}, {600.0, 0.18},
      {650.0, 0.11}, {700.0, 0.06},
  };

  c.device_provenance.device_name = "Hamamatsu S13360-3050CS";
  c.device_provenance.profile_file =
      "config/s13360_3050cs_REPRESENTATIVE.json";
  c.device_provenance.schema = "ccb-sipm-device-profile/1";
  c.device_provenance.overvoltage_V = 3.0;
  c.device_provenance.temperature_C = 25.0;
  c.device_provenance.calibration_status =
      "MANUFACTURER_REPRESENTATIVE_NOT_CALIBRATED";
  c.device_provenance.primary_source_id = "SRC-HAMA-001";
  c.device_provenance.primary_source_type = "MANUFACTURER";
  c.device_provenance.primary_source_title = "S13360-3050CS product page";
  c.device_provenance.primary_source_url =
      "https://www.hamamatsu.com/jp/en/product/optical-sensors/mppc/"
      "mppc_mppc-array/S13360-3050CS.html";
  c.device_provenance.primary_source_retrieved_date = "2026-07-23";
  c.device_provenance.secondary_source_id = "SRC-HAMA-002";
  c.device_provenance.secondary_source_type = "MANUFACTURER_GUIDE";
  c.device_provenance.secondary_source_title = "MPPC technical guide section 4";
  c.device_provenance.secondary_source_url =
      "https://hub.hamamatsu.com/us/en/technical-notes/mppc-sipms/"
      "a-technical-guide-to-silicon-photomutlipliers-MPPC-Section-4.html";
  c.device_provenance.secondary_source_retrieved_date = "2026-07-23";
  c.device_provenance.covered_parameters =
      "active_area,cells,pixel_pitch,pde_curve,recovery_time_ns,"
      "dark_count_rate_hz,prompt_crosstalk_probability,"
      "delayed_crosstalk_probability,afterpulse_fast_probability,"
      "afterpulse_slow_probability";
  c.device_provenance.warning =
      "PDE points are representative/typical and not device-specific "
      "calibration; see validation matrix V-DEV-* and V-ELEC-*.";

  c.electronics_provenance.impulse_response_status =
      "ASSUMPTION_GENERIC_CRRC_NOT_MEASURED";
  c.electronics_provenance.shaper_model =
      "CR-RC(-RC) semi-gaussian, configurable stages";
  c.electronics_provenance.integrator_stages = c.shaper_integrator_stages;
  c.electronics_provenance.note =
      "Generic analytical CR-RC(-RC) impulse, peak-normalised. A measured "
      "single-PE waveform can replace it, but exact source/effective-kernel "
      "digest provenance remains independently gated.";
  return c;
}

std::string RunMetadata::render_json() const {
  std::ostringstream os;
  os << "{\n";
  os << "  \"schema\": \"" << schema << "\",\n";
  os << "  \"engine\": \"" << engine << "\",\n";
  os << "  \"engine_version\": \"" << engine_version << "\",\n";
  os << "  \"device\": {\n";
  EmitJsonString(os, "device_name", device.device_name); os << ",\n";
  EmitJsonString(os, "profile_file", device.profile_file); os << ",\n";
  EmitJsonString(os, "schema", device.schema); os << ",\n";
  os << "    \"overvoltage_V\": " << device.overvoltage_V << ",\n";
  os << "    \"temperature_C\": " << device.temperature_C << ",\n";
  EmitJsonString(os, "calibration_status", device.calibration_status); os << ",\n";
  EmitJsonString(os, "primary_source_id", device.primary_source_id); os << ",\n";
  EmitJsonString(os, "primary_source_type", device.primary_source_type); os << ",\n";
  EmitJsonString(os, "primary_source_title", device.primary_source_title); os << ",\n";
  EmitJsonString(os, "primary_source_url", device.primary_source_url); os << ",\n";
  EmitJsonString(os, "primary_source_retrieved_date",
                 device.primary_source_retrieved_date); os << ",\n";
  EmitJsonString(os, "secondary_source_id", device.secondary_source_id); os << ",\n";
  EmitJsonString(os, "secondary_source_type", device.secondary_source_type); os << ",\n";
  EmitJsonString(os, "secondary_source_title", device.secondary_source_title); os << ",\n";
  EmitJsonString(os, "secondary_source_url", device.secondary_source_url); os << ",\n";
  EmitJsonString(os, "secondary_source_retrieved_date",
                 device.secondary_source_retrieved_date); os << ",\n";
  EmitJsonString(os, "covered_parameters", device.covered_parameters); os << ",\n";
  EmitJsonString(os, "warning", device.warning); os << "\n";
  os << "  },\n";
  os << "  \"electronics\": {\n";
  EmitJsonString(os, "impulse_response_status",
                 electronics.impulse_response_status); os << ",\n";
  EmitJsonString(os, "shaper_model", electronics.shaper_model); os << ",\n";
  os << "    \"integrator_stages\": " << electronics.integrator_stages
     << ",\n";
  EmitJsonString(os, "measured_impulse_source_id",
                 electronics.measured_impulse_source_id); os << ",\n";
  EmitJsonString(os, "measured_impulse_source_url",
                 electronics.measured_impulse_source_url); os << ",\n";
  EmitJsonString(os, "measured_impulse_retrieved_date",
                 electronics.measured_impulse_retrieved_date); os << ",\n";
  EmitJsonString(os, "measured_impulse_source_hash",
                 electronics.measured_impulse_source_hash); os << ",\n";
  EmitJsonString(os, "effective_kernel_hash",
                 electronics.effective_kernel_hash); os << ",\n";
  EmitJsonString(os, "note", electronics.note); os << "\n";
  os << "  },\n";
  os << "  \"impulse_model\": \"" << impulse_model << "\",\n";
  os << "  \"pde_curve\": [";
  for (std::size_t i = 0; i < pde_curve.size(); ++i) {
    if (i) os << ", ";
    os << "[" << pde_curve[i].wavelength_nm << ", "
       << pde_curve[i].pde << "]";
  }
  os << "],\n";
  os << "  \"model_parameters\": {\n";
  os << "    \"recovery_time_ns\": " << recovery_time_ns << ",\n";
  os << "    \"dark_count_rate_hz\": " << dark_count_rate_hz << ",\n";
  os << "    \"prompt_crosstalk_probability\": "
     << prompt_crosstalk_probability << ",\n";
  os << "    \"delayed_crosstalk_probability\": "
     << delayed_crosstalk_probability << ",\n";
  os << "    \"afterpulse_fast_probability\": "
     << afterpulse_fast_probability << ",\n";
  os << "    \"afterpulse_slow_probability\": "
     << afterpulse_slow_probability << ",\n";
  os << "    \"adc_bits\": " << adc_bits << ",\n";
  os << "    \"adc_lsb_pe\": " << adc_lsb_pe << ",\n";
  os << "    \"baseline_adc\": " << baseline_adc << ",\n";
  os << "    \"sample_dt_ns\": " << sample_dt_ns << ",\n";
  os << "    \"window_start_ns\": " << window_start_ns << ",\n";
  os << "    \"window_end_ns\": " << window_end_ns << ",\n";
  os << "    \"history_start_ns\": " << history_start_ns << "\n";
  os << "  }\n";
  os << "}\n";
  return os.str();
}

}  // namespace ccb::sipm
