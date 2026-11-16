// MESSAGE NAMED_VALUE_STRING support class

#pragma once

namespace mavlink {
namespace ardupilotmega {
namespace msg {

/**
 * @brief NAMED_VALUE_STRING message
 *
 * Send a key-value pair as string. The use of this message is discouraged for normal packets, but a quite efficient way for testing new messages and getting experimental debug output.
 */
struct NAMED_VALUE_STRING : mavlink::Message {
    static constexpr msgid_t MSG_ID = 11060;
    static constexpr size_t LENGTH = 78;
    static constexpr size_t MIN_LENGTH = 78;
    static constexpr uint8_t CRC_EXTRA = 162;
    static constexpr auto NAME = "NAMED_VALUE_STRING";


    uint32_t time_boot_ms; /*< [ms] Timestamp (time since system boot). */
    std::array<char, 10> name; /*<  Name of the debug variable */
    std::array<char, 64> value; /*<  Value of the debug variable */


    inline std::string get_name(void) const override
    {
            return NAME;
    }

    inline Info get_message_info(void) const override
    {
            return { MSG_ID, LENGTH, MIN_LENGTH, CRC_EXTRA };
    }

    inline std::string to_yaml(void) const override
    {
        std::stringstream ss;

        ss << NAME << ":" << std::endl;
        ss << "  time_boot_ms: " << time_boot_ms << std::endl;
        ss << "  name: \"" << to_string(name) << "\"" << std::endl;
        ss << "  value: \"" << to_string(value) << "\"" << std::endl;

        return ss.str();
    }

    inline void serialize(mavlink::MsgMap &map) const override
    {
        map.reset(MSG_ID, LENGTH);

        map << time_boot_ms;                  // offset: 0
        map << name;                          // offset: 4
        map << value;                         // offset: 14
    }

    inline void deserialize(mavlink::MsgMap &map) override
    {
        map >> time_boot_ms;                  // offset: 0
        map >> name;                          // offset: 4
        map >> value;                         // offset: 14
    }
};

} // namespace msg
} // namespace ardupilotmega
} // namespace mavlink
