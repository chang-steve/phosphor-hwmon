#pragma once

#include <experimental/filesystem>
#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/log.hpp>
#include <xyz/openbmc_project/Sensor/Device/error.hpp>
#include "fan_speed.hpp"
#include "fan_pwm.hpp"

/** @class Targets
 *  @brief Target type traits.
 *
 *  @tparam T - The target type.
 */
template <typename T>
struct Targets
{
    static void fail()
    {
        static_assert(sizeof(Targets) == -1, "Unsupported Target type");
    }
};

/**@brief Targets specialization for fan speed. */
template <>
struct Targets<hwmon::FanSpeed>
{
    static constexpr InterfaceType type = InterfaceType::FAN_SPEED;
};

template <>
struct Targets<hwmon::FanPwm>
{
    static constexpr InterfaceType type = InterfaceType::FAN_PWM;
};

/** @brief addTarget
 *
 *  Creates the target type interface
 *
 *  @tparam T - The target type
 *
 *  @param[in] sensor - A sensor type and name
 *  @param[in] ioAccess - hwmon sysfs access object
 *  @param[in] devPath - The /sys/devices sysfs path
 *  @param[in] info - The sdbusplus server connection and interfaces
 *
 *  @return A shared pointer to the target interface object
 *          Will be empty if no interface was created
 */
template <typename T>
std::shared_ptr<T> addTarget(const SensorSet::key_type& sensor,
                             const sysfs::hwmonio::HwmonIO& ioAccess,
                             const std::string& devPath,
                             ObjectInfo& info)
{
    std::shared_ptr<T> target;
    namespace fs = std::experimental::filesystem;
    static constexpr bool deferSignals = true;

    auto& bus = *std::get<sdbusplus::bus::bus*>(info);
    auto& obj = std::get<Object>(info);
    auto& objPath = std::get<std::string>(info);
    auto type = Targets<T>::type;

    // Check if target sysfs file exists
    std::string sysfsFullPath;

    using namespace std::literals;
    const std::string pwm = "pwm"s;
    const std::string empty = ""s;

    if (InterfaceType::FAN_PWM == type)
    {
        /* We're leveraging that the sensor ID matches for PWM.
         * TODO(venture): There's a CL from intel that allows
         * this to be specified via an environment variable.
         */
        sysfsFullPath = sysfs::make_sysfs_path(ioAccess.path(),
                                               pwm,
                                               sensor.second,
                                               empty);
    }
    else
    {
        sysfsFullPath = sysfs::make_sysfs_path(ioAccess.path(),
                                               sensor.first,
                                               sensor.second,
                                               hwmon::entry::target);
    }

    if (fs::exists(sysfsFullPath))
    {
        uint32_t targetSpeed = 0;

        try
        {
            if (InterfaceType::FAN_PWM == type)
            {
                targetSpeed = ioAccess.read(
                        pwm,
                        sensor.second,
                        empty,
                        sysfs::hwmonio::retries,
                        sysfs::hwmonio::delay);
            }
            else
            {
                targetSpeed = ioAccess.read(
                        sensor.first,
                        sensor.second,
                        hwmon::entry::target,
                        sysfs::hwmonio::retries,
                        sysfs::hwmonio::delay);
            }
        }
        catch (const std::system_error& e)
        {
            using namespace phosphor::logging;
            using namespace sdbusplus::xyz::openbmc_project::
                Sensor::Device::Error;
            using metadata = xyz::openbmc_project::Sensor::
                Device::ReadFailure;

            report<ReadFailure>(
                    metadata::CALLOUT_ERRNO(e.code().value()),
                    metadata::CALLOUT_DEVICE_PATH(devPath.c_str()));

            log<level::INFO>("Logging failing sysfs file",
                    phosphor::logging::entry(
                            "FILE=%s", sysfsFullPath.c_str()));
        }

        target = std::make_shared<T>(ioAccess.path(),
                                     devPath,
                                     sensor.second,
                                     bus,
                                     objPath.c_str(),
                                     deferSignals,
                                     targetSpeed);
        auto type = Targets<T>::type;
        obj[type] = target;
    }

    return target;
}
