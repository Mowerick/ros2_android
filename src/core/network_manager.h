#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ros2_android {

/**
 * @brief Manages network configuration for DDS discovery and Cyclone DDS setup.
 *
 * This class handles network interface enumeration and Cyclone DDS XML configuration
 * generation, with caching to avoid redundant file I/O operations.
 */
class NetworkManager {
 public:
  NetworkManager();
  ~NetworkManager();

  // No copies or moves
  NetworkManager(const NetworkManager&) = delete;
  NetworkManager& operator=(const NetworkManager&) = delete;
  NetworkManager(NetworkManager&&) = delete;
  NetworkManager& operator=(NetworkManager&&) = delete;

  /**
   * @brief Set the available network interfaces.
   * @param interfaces List of network interface names (e.g., "wlan0", "eth0")
   */
  void SetNetworkInterfaces(const std::vector<std::string>& interfaces);

  /**
   * @brief Get the currently configured network interfaces.
   * @return Vector of interface names
   */
  const std::vector<std::string>& GetNetworkInterfaces() const;

  /**
   * @brief Generate Cyclone DDS configuration file if needed.
   *
   * This method writes the cyclonedds.xml file only if the configuration has changed
   * (different domain ID or network interface) to avoid redundant file operations.
   *
   * @param cache_dir Directory to write cyclonedds.xml (typically app cache dir)
   * @param ros_domain_id ROS 2 domain ID (0-101 for localhost, 102-232 for network)
   * @param network_interface Network interface name for DDS discovery
   * @return true if configuration was generated successfully
   */
  bool GenerateCycloneDdsConfig(const std::string& cache_dir,
                                 int32_t ros_domain_id,
                                 const std::string& network_interface);

  /**
   * @brief Get the full path to the generated Cyclone DDS configuration file.
   * @return Path to cyclonedds.xml, or empty string if not yet generated
   */
  std::string GetCycloneDdsConfigPath() const;

 private:
  std::vector<std::string> network_interfaces_;

  // Cached configuration state to detect changes
  std::string last_cache_dir_;
  int32_t last_domain_id_ = -1;
  std::string last_network_interface_;
  std::string config_path_;

  /**
   * @brief Check if configuration has changed since last generation.
   */
  bool HasConfigChanged(const std::string& cache_dir,
                        int32_t ros_domain_id,
                        const std::string& network_interface) const;

  /**
   * @brief Write the Cyclone DDS XML configuration file.
   */
  bool WriteConfigFile(const std::string& path,
                       const std::string& network_interface) const;
};

}  // namespace ros2_android
