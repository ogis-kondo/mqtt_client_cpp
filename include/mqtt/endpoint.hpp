// Copyright Takatoshi Kondo 2015-2016
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#if !defined(MQTT_ENDPOINT_HPP)
#define MQTT_ENDPOINT_HPP

#include <string>
#include <vector>
#include <deque>
#include <functional>
#include <set>
#include <memory>
#include <mutex>

#include <boost/any.hpp>
#include <boost/optional.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/system/error_code.hpp>

#include <mqtt/fixed_header.hpp>
#include <mqtt/remaining_length.hpp>
#include <mqtt/utf8encoded_strings.hpp>
#include <mqtt/connect_flags.hpp>
#include <mqtt/encoded_length.hpp>
#include <mqtt/will.hpp>
#include <mqtt/session_present.hpp>
#include <mqtt/qos.hpp>
#include <mqtt/publish.hpp>
#include <mqtt/connect_return_code.hpp>
#include <mqtt/exception.hpp>

namespace mqtt {

namespace as = boost::asio;
namespace mi = boost::multi_index;

template <typename Socket, typename Strand, typename Mutex = std::mutex, template<typename...> class LockGuard = std::lock_guard>
class endpoint : public std::enable_shared_from_this<endpoint<Socket, Strand, Mutex, LockGuard>> {
    using this_type = endpoint<Socket, Strand>;
public:
    using async_handler_t = std::function<void(boost::system::error_code const& ec)>;

    /**
     * @brief Constructor for client
     */
    endpoint(as::io_service& ios)
        :strand_(ios),
         connected_(false),
         clean_session_(false),
         packet_id_master_(0),
         auto_pub_response_(true),
         auto_pub_response_async_(false)
    {}

    /**
     * @brief Constructor for server.
     *        socket should have already been connected with another endpoint.
     */
    endpoint(std::unique_ptr<Socket>&& socket)
        :strand_(socket->get_io_service()),
         socket_(std::move(socket)),
         connected_(true),
         clean_session_(false),
         packet_id_master_(0),
         auto_pub_response_(true),
         auto_pub_response_async_(false)
    {}

    /**
     * @breif Close handler
     */
    using close_handler = std::function<void()>;

    /**
     * @breif Error handler
     * @param ec error code
     */
    using error_handler = std::function<void(boost::system::error_code const& ec)>;

    /**
     * @breif Connect handler
     * @param client_id
     *        User Name.<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc385349245<BR>
     *        3.1.3.4 User Name
     * @param username
     *        User Name.<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc385349245<BR>
     *        3.1.3.4 User Name
     * @param password
     *        Password.<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc385349246<BR>
     *        3.1.3.5 Password
     * @param will
     *        Will. It contains retain, QoS, topic, and message.<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc385349232<BR>
     *        3.1.2.5 Will Flag<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc385349233<BR>
     *        3.1.2.6 Will QoS<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc385349234<BR>
     *        3.1.2.7 Will Retain<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc385349243<BR>
     *        3.1.3.2 Will Topic<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc385349244<BR>
     *        3.1.3.3 Will Message<BR>
     * @param clean_session
     *        Clean Session<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc385349231<BR>
     *        3.1.2.4 Clean Session
     * @param keep_alive
     *        Keep Alive<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc385349237<BR>
     *        3.1.2.10 Keep Alive
     * @return if the handler returns true, then continue receiving, otherwise quit.
     *
     */
    using connect_handler = std::function<
        bool(std::string const& client_id,
             boost::optional<std::string> const& username,
             boost::optional<std::string> const& password,
             boost::optional<will> will,
             bool clean_session,
             std::uint16_t keep_alive)>;

    /**
     * @breif Connack handler
     * @param session_present
     *        Session present flag.<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718035<BR>
     *        3.2.2.2 Session Present
     * @param return_code
     *        connect_return_code<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718035<BR>
     *        3.2.2.3 Connect Return code
     * @return if the handler returns true, then continue receiving, otherwise quit.
     */
    using connack_handler = std::function<bool(bool session_present, std::uint8_t return_code)>;

    /**
     * @breif Publish handler
     * @param fixed_header
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718038<BR>
     *        3.3.1 Fixed header<BR>
     *        You can check the fixed header using mqtt::publish functions.
     * @param packet_id
     *        packet identifier<BR>
     *        If received publish's QoS is 0, packet_id is boost::none.<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718039<BR>
     *        3.3.2  Variable header
     * @param topic_name
     *        Topic name
     * @param contents
     *        Published contents
     * @return if the handler returns true, then continue receiving, otherwise quit.
     */
    using publish_handler = std::function<bool(std::uint8_t fixed_header,
                                               boost::optional<std::uint16_t> packet_id,
                                               std::string topic_name,
                                               std::string contents)>;

    /**
     * @breif Puback handler
     * @param packet_id
     *        packet identifier<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718045<BR>
     *        3.4.2 Variable header
     * @return if the handler returns true, then continue receiving, otherwise quit.
     */
    using puback_handler = std::function<bool(std::uint16_t packet_id)>;

    /**
     * @breif Pubrec handler
     * @param packet_id
     *        packet identifier<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718050<BR>
     *        3.5.2 Variable header
     * @return if the handler returns true, then continue receiving, otherwise quit.
     */
    using pubrec_handler = std::function<bool(std::uint16_t packet_id)>;

    /**
     * @breif Pubrel handler
     * @param packet_id
     *        packet identifier<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc385349791<BR>
     *        3.6.2 Variable header
     * @return if the handler returns true, then continue receiving, otherwise quit.
     */
    using pubrel_handler = std::function<bool(std::uint16_t packet_id)>;

    /**
     * @breif Pubcomp handler
     * @param packet_id
     *        packet identifier<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718060<BR>
     *        3.7.2 Variable header
     * @return if the handler returns true, then continue receiving, otherwise quit.
     */
    using pubcomp_handler = std::function<bool(std::uint16_t packet_id)>;

    /**
     * @breif Publish response sent handler
     *        This function is called just after puback sent on QoS1, or pubcomp sent on QoS2.
     * @param packet_id
     *        packet identifier<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718060<BR>
     *        3.7.2 Variable header
     */
    using pub_res_sent_handler = std::function<void(std::uint16_t packet_id)>;

    /**
     * @breif Subscribe handler
     * @param packet_id packet identifier<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc385349801<BR>
     *        3.8.2 Variable header
     * @param entries
     *        Collection of a pair of Topic Filter and QoS.<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc385349802<BR>
     * @return if the handler returns true, then continue receiving, otherwise quit.
     */
    using subscribe_handler = std::function<bool(std::uint16_t packet_id,
                                                 std::vector<std::tuple<std::string, std::uint8_t>> entries)>;

    /**
     * @breif Suback handler
     * @param packet_id packet identifier<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718070<BR>
     *        3.9.2 Variable header
     * @param qoss
     *        Collection of QoS that is corresponding to subscribed topic order.<BR>
     *        If subscription is failure, the value is boost::none.<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718071<BR>
     * @return if the handler returns true, then continue receiving, otherwise quit.
     */
    using suback_handler = std::function<bool(std::uint16_t packet_id,
                                              std::vector<boost::optional<std::uint8_t>> qoss)>;

    /**
     * @breif Unsubscribe handler
     * @param packet_id packet identifier<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc385349810<BR>
     *        3.10.2 Variable header
     * @param topics
     *        Collection of Topic Filters<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc384800448<BR>
     * @return if the handler returns true, then continue receiving, otherwise quit.
     */
    using unsubscribe_handler = std::function<bool(std::uint16_t packet_id,
                                                   std::vector<std::string> topics)>;

    /**
     * @breif Unsuback handler
     * @param packet_id packet identifier<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718045<BR>
     *        3.11.2 Variable header
     * @return if the handler returns true, then continue receiving, otherwise quit.
     */
    using unsuback_handler = std::function<bool(std::uint16_t)>;

    /**
     * @breif Pingreq handler
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718081<BR>
     *        3.13 PINGREQ – PING request
     * @return if the handler returns true, then continue receiving, otherwise quit.
     */
    using pingreq_handler = std::function<bool()>;

    /**
     * @breif Pingresp handler
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718086<BR>
     *        3.13 PINGRESP – PING response
     * @return if the handler returns true, then continue receiving, otherwise quit.
     */
    using pingresp_handler = std::function<bool()>;

    /**
     * @breif Disconnect handler
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc384800463<BR>
     *        3.14 DISCONNECT – Disconnect notification
     */
    using disconnect_handler = std::function<void()>;

    endpoint(endpoint&&) = delete;

    endpoint& operator=(endpoint&&) = delete;

    /**
     * @breif Set endpoint id.
     * @param id endpoint id
     *
     * This function should be called before calling connect().<BR>
     * See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718031<BR>
     * 3.1.3.1 Client Identifier
     */
    void set_client_id(std::string id) {
        client_id_ = std::move(id);
    }

    /**
     * @breif Set clean session.
     * @param cs clean session
     *
     * This function should be called before calling connect().<BR>
     * See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718029<BR>
     * 3.1.2.4 Clean Session<BR>
     * After constructing a endpoint, the clean session is set to false.
     */
    void set_clean_session(bool cs) {
        clean_session_ = cs;
    }

    /**
     * @breif Set username.
     * @param name username
     *
     * This function should be called before calling connect().<BR>
     * See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718031<BR>
     * 3.1.3.4 User Name
     */
    void set_user_name(std::string name) {
        user_name_ = std::move(name);
    }

    /**
     * @breif Set password.
     * @param password password
     *
     * This function should be called before calling connect().<BR>
     * See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718031<BR>
     * 3.1.3.5 Password
     */
    void set_password(std::string password) {
        password_ = std::move(password);
    }

    /**
     * @breif Set will.
     * @param w will
     *
     * This function should be called before calling connect().<BR>
     * 'will' would be send when endpoint is disconnected without calling disconnect().
     */
    void set_will(will w) {
        will_ = std::move(w);
    }

    /**
     * @breif Set auto publish response mode.
     * @param b set value
     *
     * When set auto publish response mode to true, puback, pubrec, pubrel,and pub comp automatically send.<BR>
     */
    void set_auto_pub_response(bool b = true, bool async = true) {
        auto_pub_response_ = b;
        auto_pub_response_async_ = async;
    }

    /**
     * @brief Set close handler
     * @param h handler
     */
    void set_close_handler(close_handler h) {
        h_close_ = std::move(h);
    }

    /**
     * @brief Set error handler
     * @param h handler
     */
    void set_error_handler(error_handler h) {
        h_error_ = std::move(h);
    }

    /**
     * @brief Set connect handler
     * @param h handler
     */
    void set_connect_handler(connect_handler h) {
        h_connect_ = std::move(h);
    }

    /**
     * @brief Set connack handler
     * @param h handler
     */
    void set_connack_handler(connack_handler h) {
        h_connack_ = std::move(h);
    }

    /**
     * @brief Set puback handler
     * @param h handler
     */
    void set_publish_handler(publish_handler h) {
        h_publish_ = std::move(h);
    }

    /**
     * @brief Set puback handler
     * @param h handler
     */
    void set_puback_handler(puback_handler h) {
        h_puback_ = std::move(h);
    }

    /**
     * @brief Set pubrec handler
     * @param h handler
     */
    void set_pubrec_handler(pubrec_handler h) {
        h_pubrec_ = std::move(h);
    }

    /**
     * @brief Set pubrel handler
     * @param h handler
     */
    void set_pubrel_handler(pubrel_handler h) {
        h_pubrel_ = std::move(h);
    }

    /**
     * @brief Set pubcomp handler
     * @param h handler
     */
    void set_pubcomp_handler(pubcomp_handler h) {
        h_pubcomp_ = std::move(h);
    }

    /**
     * @brief Set pubcomp handler
     * @param h handler
     */
    void set_pub_res_sent_handler(pub_res_sent_handler h) {
        h_pub_res_sent_ = std::move(h);
    }

    /**
     * @brief Set subscribe handler
     * @param h handler
     */
    void set_subscribe_handler(subscribe_handler h) {
        h_subscribe_ = std::move(h);
    }

    /**
     * @brief Set suback handler
     * @param h handler
     */
    void set_suback_handler(suback_handler h) {
        h_suback_ = std::move(h);
    }

    /**
     * @brief Set unsubscribe handler
     * @param h handler
     */
    void set_unsubscribe_handler(unsubscribe_handler h) {
        h_unsubscribe_ = std::move(h);
    }

    /**
     * @brief Set unsuback handler
     * @param h handler
     */
    void set_unsuback_handler(unsuback_handler h) {
        h_unsuback_ = std::move(h);
    }

    /**
     * @brief Set pingreq handler
     * @param h handler
     */
    void set_pingreq_handler(pingreq_handler h) {
        h_pingreq_ = std::move(h);
    }

    /**
     * @brief Set pingresp handler
     * @param h handler
     */
    void set_pingresp_handler(pingresp_handler h) {
        h_pingresp_ = std::move(h);
    }

    /**
     * @brief Set disconnect handler
     * @param h handler
     */
    void set_disconnect_handler(disconnect_handler h) {
        h_disconnect_ = std::move(h);
    }

    /**
     * @brief start session with a connected endpoint.
     * @param func finish handler that is called when the session is finished
     *
     */
    void start_session(async_handler_t const& func = async_handler_t()) {
        async_read_control_packet_type(func);
    }

    // Blocking APIs

    /**
     * @brief Publish QoS0
     * @param topic_name
     *        A topic name to publish
     * @param contents
     *        The contents to publish
     * @param retain
     *        A retain flag. If set it to true, the contents is retained.<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718038<BR>
     *        3.3.1.3 RETAIN
     */
    void publish_at_most_once(
        std::string const& topic_name,
        std::string const& contents,
        bool retain = false) {
        send_publish(topic_name, qos::at_most_once, retain, false, 0, contents);
    }

    /**
     * @brief Publish QoS1
     * @param topic_name
     *        A topic name to publish
     * @param contents
     *        The contents to publish
     * @param retain
     *        A retain flag. If set it to true, the contents is retained.<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718038<BR>
     *        3.3.1.3 RETAIN
     * @return packet_id
     * packet_id is automatically generated.
     */
    std::uint16_t publish_at_least_once(
        std::string const& topic_name,
        std::string const& contents,
        bool retain = false) {
        std::uint16_t packet_id = acquire_unique_packet_id();
        send_publish(topic_name, qos::at_least_once, retain, false, packet_id, contents);
        return packet_id;
    }

    /**
     * @brief Publish QoS2
     * @param topic_name
     *        A topic name to publish
     * @param contents
     *        The contents to publish
     * @param retain
     *        A retain flag. If set it to true, the contents is retained.<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718038<BR>
     *        3.3.1.3 RETAIN
     * @return packet_id
     * packet_id is automatically generated.
     */
    std::uint16_t publish_exactly_once(
        std::string const& topic_name,
        std::string const& contents,
        bool retain = false) {
        std::uint16_t packet_id = acquire_unique_packet_id();
        send_publish(topic_name, qos::exactly_once, retain, false, packet_id, contents);
        return packet_id;
    }

    /**
     * @brief Publish
     * @param topic_name
     *        A topic name to publish
     * @param contents
     *        The contents to publish
     * @param qos
     *        mqtt::qos
     * @param retain
     *        A retain flag. If set it to true, the contents is retained.<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718038<BR>
     *        3.3.1.3 RETAIN
     * @return packet_id. If qos is set to at_most_once, return 0.
     * packet_id is automatically generated.
     */
    std::uint16_t publish(
        std::string const& topic_name,
        std::string const& contents,
        std::uint8_t qos = qos::at_most_once,
        bool retain = false) {
        std::uint16_t packet_id = qos == 0 ? 0 : acquire_unique_packet_id();
        send_publish(topic_name, qos, retain, false, packet_id, contents);
        return packet_id;
    }

    /**
     * @brief Subscribe
     * @param topic_name
     *        A topic name to subscribe
     * @param qos
     *        mqtt::qos
     * @param args
     *        args should be zero of more pairs of topic_name and qos.
     * @return packet_id. If qos is set to at_most_once, return 0.
     * packet_id is automatically generated.<BR>
     * You can subscribe multiple topics all at once.<BR>
     * See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718066
     */
    template <typename... Args>
    std::uint16_t subscribe(
        std::string const& topic_name,
        std::uint8_t qos, Args... args) {
        std::uint16_t packet_id = acquire_unique_packet_id();
        std::vector<std::tuple<std::reference_wrapper<std::string const>, std::uint8_t>> params;
        params.reserve((sizeof...(args) + 2) / 2);
        send_subscribe(params, packet_id, topic_name, qos, args...);
        return packet_id;
    }

    /**
     * @brief Subscribe
     * @param params a vector of the topic_filter and qos pair.
     * @return packet_id. If qos is set to at_most_once, return 0.
     * packet_id is automatically generated.<BR>
     * You can subscribe multiple topics all at once.<BR>
     * See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718066
     */
    std::uint16_t subscribe(
        std::vector<std::tuple<std::string, std::uint8_t>> const& params
    ) {
        std::uint16_t packet_id = acquire_unique_packet_id();
        std::vector<std::tuple<std::reference_wrapper<std::string const>, std::uint8_t>> rparams;
        rparams.reserve(params.size());
        for (auto const& e : params) {
            rparams.emplace_back(std::get<0>(e), std::get<1>(e));
        }
        send_subscribe(rparams, packet_id);
        return packet_id;
    }

    /**
     * @brief Unsubscribe
     * @param topic_name
     *        A topic name to subscribe
     * @param args
     *        args should be zero or more topics
     * @return packet_id. If qos is set to at_most_once, return 0.
     * packet_id is automatically generated.<BR>
     * You can subscribe multiple topics all at once.<BR>
     * See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718066
     */
    template <typename... Args>
    std::uint16_t unsubscribe(
        std::string const& topic_name,
        Args... args) {
        std::uint16_t packet_id = acquire_unique_packet_id();
        std::vector<std::reference_wrapper<std::string const>> params;
        params.reserve(sizeof...(args) + 1);
        send_unsubscribe(params, packet_id, topic_name, args...);
        return packet_id;
    }

    /**
     * @brief Unsubscribe
     * @param params a collection of topic_filter.
     * @return packet_id. If qos is set to at_most_once, return 0.
     * packet_id is automatically generated.<BR>
     * You can subscribe multiple topics all at once.<BR>
     * See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718066
     */
    std::uint16_t unsubscribe(
        std::vector<std::string> const& params
    ) {
        std::uint16_t packet_id = acquire_unique_packet_id();
        std::vector<std::reference_wrapper<std::string const>> rparams;
        rparams.reserve(params.size());
        for (auto const& e : params) {
            rparams.emplace_back(e);
        }
        send_unsubscribe(rparams, packet_id);
        return packet_id;
    }

    /**
     * @brief Disconnect
     * Send a disconnect packet to the connected broker. It is a clean disconnecting sequence.
     * The broker disconnects the endpoint after receives the disconnect packet.<BR>
     * When the endpoint disconnects using disconnect(), a will won't send.<BR>
     * See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718090<BR>
     */
    void disconnect() {
        if (connected_) {
            send_disconnect();
        }
    }

    /**
     * @brief Disconnect by endpoint
     * Force disconnect. It is not a clean disconnect sequence.<BR>
     * When the endpoint disconnects using force_disconnect(), a will will send.<BR>
     */
    void force_disconnect() {
        if (connected_) {
            shutdown(*socket_);
            connected_ = false;
        }
    }


    // packet_id manual setting version

    /**
     * @brief Publish QoS1 with a manual set packet identifier
     * @param packet_id
     *        packet identifier
     * @param topic_name
     *        A topic name to publish
     * @param contents
     *        The contents to publish
     * @param retain
     *        A retain flag. If set it to true, the contents is retained.<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718038<BR>
     *        3.3.1.3 RETAIN
     * @return If packet_id is used in the publishing/subscribing sequence, then returns false and
     *         contents doesn't publish, otherwise return true and contents publish.
     */
    bool publish_at_least_once(
        std::uint16_t packet_id,
        std::string const& topic_name,
        std::string const& contents,
        bool retain = false) {
        if (register_packet_id(packet_id)) {
            send_publish(topic_name, qos::at_least_once, retain, false, packet_id, contents);
            return true;
        }
        return false;
    }

    /**
     * @brief Publish QoS2 with a manual set packet identifier
     * @param packet_id
     *        packet identifier
     * @param topic_name
     *        A topic name to publish
     * @param contents
     *        The contents to publish
     * @param retain
     *        A retain flag. If set it to true, the contents is retained.<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718038<BR>
     *        3.3.1.3 RETAIN
     * @return If packet_id is used in the publishing/subscribing sequence, then returns false and
     *         contents doesn't publish, otherwise return true and contents publish.
     */
    bool publish_exactly_once(
        std::uint16_t packet_id,
        std::string const& topic_name,
        std::string const& contents,
        bool retain = false) {
        if (register_packet_id(packet_id)) {
            send_publish(topic_name, qos::exactly_once, retain, false, packet_id, contents);
            return true;
        }
        return false;
    }

    /**
     * @brief Publish with a manual set packet identifier
     * @param packet_id
     *        packet identifier
     * @param topic_name
     *        A topic name to publish
     * @param contents
     *        The contents to publish
     * @param qos
     *        mqtt::qos
     * @param retain
     *        A retain flag. If set it to true, the contents is retained.<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718038<BR>
     *        3.3.1.3 RETAIN
     * @return If packet_id is used in the publishing/subscribing sequence, then returns false and
     *         contents don't publish, otherwise return true and contents publish.
     */
    bool publish(
        std::uint16_t packet_id,
        std::string const& topic_name,
        std::string const& contents,
        std::uint8_t qos = qos::at_most_once,
        bool retain = false) {
        if (register_packet_id(packet_id)) {
            send_publish(topic_name, qos, retain, false, packet_id, contents);
            return true;
        }
        return false;
    }

    /**
     * @brief Publish as dup with a manual set packet identifier
     * @param packet_id
     *        packet identifier
     * @param topic_name
     *        A topic name to publish
     * @param contents
     *        The contents to publish
     * @param qos
     *        mqtt::qos
     * @param retain
     *        A retain flag. If set it to true, the contents is retained.<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718038<BR>
     *        3.3.1.3 RETAIN
     * @return If packet_id is used in the publishing/subscribing sequence, then returns false and
     *         contents don't publish, otherwise return true and contents publish.
     */
    bool publish_dup(
        std::uint16_t packet_id,
        std::string const& topic_name,
        std::string const& contents,
        std::uint8_t qos = qos::at_most_once,
        bool retain = false) {
        if (register_packet_id(packet_id)) {
            send_publish(topic_name, qos, retain, true, packet_id, contents);
            return true;
        }
        return false;
    }

    /**
     * @brief Subscribe with a manual set packet identifier
     * @param packet_id
     *        packet identifier
     * @param topic_name
     *        A topic name to subscribe
     * @param qos
     *        mqtt::qos
     * @param args
     *        args should be zero of more pairs of topic_name and qos.
     * @return If packet_id is used in the publishing/subscribing sequence, then returns false and
     *         doesn't subscribe, otherwise return true and subscribes.
     * You can subscribe multiple topics all at once.<BR>
     * See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718066<BR>
     */
    template <typename... Args>
    bool subscribe(
        std::uint16_t packet_id,
        std::string const& topic_name,
        std::uint8_t qos, Args... args) {
        if (register_packet_id(packet_id)) {
            std::vector<std::tuple<std::reference_wrapper<std::string const>, std::uint8_t>> params;
            params.reserve((sizeof...(args) + 2) / 2);
            send_subscribe(params, packet_id, topic_name, qos, args...);
            return true;
        }
        return false;
    }

    /**
     * @brief Subscribe with a manual set packet identifier
     * @param packet_id
     *        packet identifier
     * @param params a vector of the topic_filter and qos pair.
     * @return If packet_id is used in the publishing/subscribing sequence, then returns false and
     *         doesn't subscribe, otherwise return true and subscribes.
     * You can subscribe multiple topics all at once.<BR>
     * See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718066<BR>
     */
    bool subscribe(
        std::uint16_t packet_id,
        std::vector<std::tuple<std::string, std::uint8_t>> const& params
    ) {
        if (register_packet_id(packet_id)) {
            std::vector<std::tuple<std::reference_wrapper<std::string const>, std::uint8_t>> rparams;
            rparams.reserve(params.size());
            for (auto const& e : params) {
                rparams.emplace_back(std::get<0>(e), std::get<1>(e));
            }
            send_subscribe(rparams, packet_id);
            return true;
        }
        return false;
    }

    /**
     * @brief Unsubscribe with a manual set packet identifier
     * @param packet_id
     *        packet identifier
     * @param topic_name
     *        A topic name to subscribe
     * @param args
     *        args should be zero or more topics
     * @return If packet_id is used in the publishing/subscribing sequence, then returns false and
     *         doesn't unsubscribe, otherwise return true and unsubscribes.
     * You can subscribe multiple topics all at once.<BR>
     * See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718066
     */
    template <typename... Args>
    bool unsubscribe(
        std::uint16_t packet_id,
        std::string const& topic_name,
        Args... args) {
        if (register_packet_id(packet_id)) {
            std::vector<std::reference_wrapper<std::string const>> params;
            params.reserve(sizeof...(args) + 1);
            send_unsubscribe(params, packet_id, topic_name, args...);
            return true;
        }
        return false;
    }

    /**
     * @brief Unsubscribe with a manual set packet identifier
     * @param packet_id
     *        packet identifier
     * @param params a collection of topic_filter
     * @return If packet_id is used in the publishing/subscribing sequence, then returns false and
     *         doesn't unsubscribe, otherwise return true and unsubscribes.
     * You can subscribe multiple topics all at once.<BR>
     * See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718066
     */
    bool unsubscribe(
        std::uint16_t packet_id,
        std::vector<std::string> const& params
    ) {
        if (register_packet_id(packet_id)) {
            std::vector<std::reference_wrapper<std::string const>> rparams;
            rparams.reserve(params.size());
            for (auto const& e : params) {
                rparams.emplace_back(e);
            }
            send_unsubscribe(rparams, packet_id);
            return true;
        }
        return false;
    }

    void pingreq() {
        send_pingreq();
    }

    void connect(std::uint16_t keep_alive_sec) {
        send_connect(keep_alive_sec);
    }

    // For broker side
    void connack(bool session_present, std::uint8_t return_code) {
        send_connack(session_present, return_code);
    }

    void puback(std::uint16_t packet_id) {
        send_puback(packet_id);
    }

    void pubrec(std::uint16_t packet_id) {
        send_pubrec(packet_id);
    }

    void pubrel(std::uint16_t packet_id) {
        send_pubrel(packet_id);
    }

    void pubcomp(std::uint16_t packet_id) {
        send_pubcomp(packet_id);
    }

    template <typename... Args>
    void suback(
        std::uint16_t packet_id,
        std::uint8_t qos, Args&&... args) {
        std::vector<std::uint8_t> params;
        send_suback(params, packet_id, qos, std::forward<Args>(args)...);
    }

    void suback(
        std::uint16_t packet_id,
        std::vector<std::uint8_t> const& qoss) {
        send_suback(qoss, packet_id);
    }

    void unsuback(
        std::uint16_t packet_id) {
        send_unsuback(packet_id);
    }

    void pingresp() {
        send_pingresp();
    }


    /**
     * @brief Publish QoS0
     * @param topic_name
     *        A topic name to publish
     * @param contents
     *        The contents to publish
     * @param retain
     *        A retain flag. If set it to true, the contents is retained.<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718038<BR>
     *        3.3.1.3 RETAIN
     */
    void async_publish_at_most_once(
        std::string const& topic_name,
        std::string const& contents,
        bool retain = false,
        async_handler_t const& func = async_handler_t()) {
        async_send_publish(topic_name, qos::at_most_once, retain, false, 0, contents, func);
    }

    /**
     * @brief Publish QoS1
     * @param topic_name
     *        A topic name to publish
     * @param contents
     *        The contents to publish
     * @param retain
     *        A retain flag. If set it to true, the contents is retained.<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718038<BR>
     *        3.3.1.3 RETAIN
     * @return packet_id
     * packet_id is automatically generated.
     */
    std::uint16_t async_publish_at_least_once(
        std::string const& topic_name,
        std::string const& contents,
        bool retain = false,
        async_handler_t const& func = async_handler_t()) {
        std::uint16_t packet_id = acquire_unique_packet_id();
        async_send_publish(topic_name, qos::at_least_once, retain, false, packet_id, contents, func);
        return packet_id;
    }

    /**
     * @brief Publish QoS2
     * @param topic_name
     *        A topic name to publish
     * @param contents
     *        The contents to publish
     * @param retain
     *        A retain flag. If set it to true, the contents is retained.<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718038<BR>
     *        3.3.1.3 RETAIN
     * @return packet_id
     * packet_id is automatically generated.
     */
    std::uint16_t async_publish_exactly_once(
        std::string const& topic_name,
        std::string const& contents,
        bool retain = false,
        async_handler_t const& func = async_handler_t()) {
        std::uint16_t packet_id = acquire_unique_packet_id();
        async_send_publish(topic_name, qos::exactly_once, retain, false, packet_id, contents, func);
        return packet_id;
    }

    /**
     * @brief Publish
     * @param topic_name
     *        A topic name to publish
     * @param contents
     *        The contents to publish
     * @param qos
     *        mqtt::qos
     * @param retain
     *        A retain flag. If set it to true, the contents is retained.<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718038<BR>
     *        3.3.1.3 RETAIN
     * @return packet_id. If qos is set to at_most_once, return 0.
     * packet_id is automatically generated.
     */
    std::uint16_t async_publish(
        std::string const& topic_name,
        std::string const& contents,
        std::uint8_t qos = qos::at_most_once,
        bool retain = false,
        async_handler_t const& func = async_handler_t()) {
        std::uint16_t packet_id = qos == 0 ? 0 : acquire_unique_packet_id();
        async_send_publish(topic_name, qos, retain, false, packet_id, contents, func);
        return packet_id;
    }

    /**
     * @brief Subscribe
     * @param topic_name
     *        A topic name to subscribe
     * @param qos
     *        mqtt::qos
     * @param args
     *        args should be zero of more pairs of topic_name and qos.
     * @return packet_id. If qos is set to at_most_once, return 0.
     * packet_id is automatically generated.<BR>
     * You can subscribe multiple topics all at once.<BR>
     * See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718066
     */
    template <typename... Args>
    typename std::enable_if<
        sizeof...(Args) != 0,
        std::uint16_t
    >::type
    async_subscribe(
        std::string const& topic_name,
        std::uint8_t qos, Args&&... args) {
        return async_subscribe_imp(topic_name, qos, std::forward<Args>(args)...);
    }

    std::uint16_t async_subscribe(
        std::string const& topic_name,
        std::uint8_t qos,
        async_handler_t const& func = async_handler_t()) {
        std::uint16_t packet_id = acquire_unique_packet_id();
        std::vector<std::tuple<std::reference_wrapper<std::string const>, std::uint8_t>> params;
        params.reserve(1);
        async_send_subscribe(params, packet_id, topic_name, qos, func);
        return packet_id;
    }

    std::uint16_t async_subscribe(
        std::vector<std::tuple<std::string, std::uint8_t>> const& params,
        async_handler_t const& func = async_handler_t()) {
        std::uint16_t packet_id = acquire_unique_packet_id();
        std::vector<std::tuple<std::reference_wrapper<std::string const>, std::uint8_t>> rparams;
        rparams.reserve(params.size());
        for (auto const& e : params) {
            rparams.emplace_back(std::get<0>(e), std::get<1>(e));
        }
        async_send_subscribe(rparams, packet_id, func);
        return packet_id;
    }

    /**
     * @brief Unsubscribe
     * @param topic_name
     *        A topic name to subscribe
     * @param args
     *        args should be zero or more topics
     * @return packet_id. If qos is set to at_most_once, return 0.
     * packet_id is automatically generated.<BR>
     * You can subscribe multiple topics all at once.<BR>
     * See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718066
     */
    template <typename... Args>
    typename std::enable_if<
        sizeof...(Args) != 0,
        std::uint16_t
    >::type
    async_unsubscribe(
        std::string const& topic_name,
        Args&&... args) {
        return async_unsubscribe_imp(topic_name, std::forward<Args>(args)...);
    }

    std::uint16_t async_unsubscribe(
        std::string const& topic_name,
        async_handler_t const& func = async_handler_t()) {
        std::uint16_t packet_id = acquire_unique_packet_id();
        std::vector<std::reference_wrapper<std::string const>> params;
        params.reserve(1);
        async_send_unsubscribe(params, packet_id, topic_name, func);
        return packet_id;
    }

    std::uint16_t async_unsubscribe(
        std::vector<std::string> const& params,
        async_handler_t const& func = async_handler_t()) {
        std::uint16_t packet_id = acquire_unique_packet_id();
        std::vector<std::reference_wrapper<std::string const>> rparams;
        rparams.reserve(params.size());
        for (auto const& e : params) {
            rparams.emplace_back(e);
        }
        async_send_unsubscribe(rparams, packet_id, func);
        return packet_id;
    }

    /**
     * @brief Disconnect
     * Send a disconnect packet to the connected broker. It is a clean disconnecting sequence.
     * The broker disconnects the endpoint after receives the disconnect packet.<BR>
     * When the endpoint disconnects using disconnect(), a will won't send.<BR>
     * See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718090<BR>
     */
    void async_disconnect(async_handler_t const& func = async_handler_t()) {
        if (connected_) {
            async_send_disconnect(func);
        }
    }

    // packet_id manual setting version

    /**
     * @brief Publish QoS1 with a manual set packet identifier
     * @param packet_id
     *        packet identifier
     * @param topic_name
     *        A topic name to publish
     * @param contents
     *        The contents to publish
     * @param retain
     *        A retain flag. If set it to true, the contents is retained.<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718038<BR>
     *        3.3.1.3 RETAIN
     * @return If packet_id is used in the publishing/subscribing sequence, then returns false and
     *         contents doesn't publish, otherwise return true and contents publish.
     */
    bool async_publish_at_least_once(
        std::uint16_t packet_id,
        std::string const& topic_name,
        std::string const& contents,
        bool retain = false,
        async_handler_t const& func = async_handler_t()) {
        if (register_packet_id(packet_id)) {
            async_send_publish(topic_name, qos::at_least_once, retain, false, packet_id, contents, func);
            return true;
        }
        return false;
    }

    /**
     * @brief Publish QoS2 with a manual set packet identifier
     * @param packet_id
     *        packet identifier
     * @param topic_name
     *        A topic name to publish
     * @param contents
     *        The contents to publish
     * @param retain
     *        A retain flag. If set it to true, the contents is retained.<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718038<BR>
     *        3.3.1.3 RETAIN
     * @return If packet_id is used in the publishing/subscribing sequence, then returns false and
     *         contents doesn't publish, otherwise return true and contents publish.
     */
    bool async_publish_exactly_once(
        std::uint16_t packet_id,
        std::string const& topic_name,
        std::string const& contents,
        bool retain = false,
        async_handler_t const& func = async_handler_t()) {
        if (register_packet_id(packet_id)) {
            async_send_publish(topic_name, qos::exactly_once, retain, false, packet_id, contents, func);
            return true;
        }
        return false;
    }

    /**
     * @brief Publish with a manual set packet identifier
     * @param packet_id
     *        packet identifier
     * @param topic_name
     *        A topic name to publish
     * @param contents
     *        The contents to publish
     * @param qos
     *        mqtt::qos
     * @param retain
     *        A retain flag. If set it to true, the contents is retained.<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718038<BR>
     *        3.3.1.3 RETAIN
     * @return If packet_id is used in the publishing/subscribing sequence, then returns false and
     *         contents don't publish, otherwise return true and contents publish.
     */
    bool async_publish(
        std::uint16_t packet_id,
        std::string const& topic_name,
        std::string const& contents,
        std::uint8_t qos = qos::at_most_once,
        bool retain = false,
        async_handler_t const& func = async_handler_t()) {
        if (register_packet_id(packet_id)) {
            async_send_publish(topic_name, qos, retain, false, packet_id, contents, func);
            return true;
        }
        return false;
    }

    /**
     * @brief Publish as dup with a manual set packet identifier
     * @param packet_id
     *        packet identifier
     * @param topic_name
     *        A topic name to publish
     * @param contents
     *        The contents to publish
     * @param qos
     *        mqtt::qos
     * @param retain
     *        A retain flag. If set it to true, the contents is retained.<BR>
     *        See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718038<BR>
     *        3.3.1.3 RETAIN
     * @return If packet_id is used in the publishing/subscribing sequence, then returns false and
     *         contents don't publish, otherwise return true and contents publish.
     */
    bool async_publish_dup(
        std::uint16_t packet_id,
        std::string const& topic_name,
        std::string const& contents,
        std::uint8_t qos = qos::at_most_once,
        bool retain = false,
        async_handler_t const& func = async_handler_t()) {
        if (register_packet_id(packet_id)) {
            async_send_publish(topic_name, qos, retain, true, packet_id, contents, func);
            return true;
        }
        return false;
    }

    /**
     * @brief Subscribe with a manual set packet identifier
     * @param packet_id
     *        packet identifier
     * @param topic_name
     *        A topic name to subscribe
     * @param qos
     *        mqtt::qos
     * @param args
     *        args should be zero of more pairs of topic_name and qos.
     * @return If packet_id is used in the publishing/subscribing sequence, then returns false and
     *         doesn't subscribe, otherwise return true and subscribes.
     * You can subscribe multiple topics all at once.<BR>
     * See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718066<BR>
     */
    template <typename... Args>
    typename std::enable_if<
        sizeof...(Args) != 0,
        bool
    >::type
    async_subscribe(
        std::uint16_t packet_id,
        std::string const& topic_name,
        std::uint8_t qos, Args&&... args) {
        return async_subscribe_imp(packet_id, topic_name, qos, std::forward<Args>(args)...);
    }

    bool async_subscribe(
        std::uint16_t packet_id,
        std::string const& topic_name,
        std::uint8_t qos,
        async_handler_t const& func = async_handler_t()) {
        if (register_packet_id(packet_id)) {
            std::vector<std::tuple<std::reference_wrapper<std::string const>, std::uint8_t>> params;
            params.reserve(1);
            async_send_subscribe(params, packet_id, topic_name, qos, func);
            return true;
        }
        return false;
    }

    bool async_subscribe(
        std::uint16_t packet_id,
        std::vector<std::tuple<std::string, std::uint8_t>> const& params,
        async_handler_t const& func = async_handler_t()) {
        if (register_packet_id(packet_id)) {
            std::vector<std::tuple<std::reference_wrapper<std::string const>, std::uint8_t>> rparams;
            rparams.reserve(params.size());
            for (auto const& e : params) {
                rparams.emplace_back(std::get<0>(e), std::get<1>(e));
            }
            async_send_subscribe(rparams, packet_id, func);
            return true;
        }
        return false;
    }

    /**
     * @brief Unsubscribe with a manual set packet identifier
     * @param packet_id
     *        packet identifier
     * @param topic_name
     *        A topic name to subscribe
     * @param args
     *        args should be zero or more topics
     * @return If packet_id is used in the publishing/subscribing sequence, then returns false and
     *         doesn't unsubscribe, otherwise return true and unsubscribes.
     * You can subscribe multiple topics all at once.<BR>
     * See http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718066
     */
    template <typename... Args>
    typename std::enable_if<
        sizeof...(Args) != 0,
        bool
    >::type
    async_unsubscribe(
        std::uint16_t packet_id,
        std::string const& topic_name,
        Args&&... args) {
        return async_unsubscribe_imp(packet_id, topic_name, std::forward<Args>(args)...);
    }

    bool async_unsubscribe(
        std::uint16_t packet_id,
        std::vector<std::string> const& params,
        async_handler_t const& func = async_handler_t()) {
        if (register_packet_id(packet_id)) {
            std::vector<std::reference_wrapper<std::string const>> rparams;
            rparams.reserve(params.size());
            for (auto const& e : params) {
                rparams.emplace_back(e);
            }
            async_send_unsubscribe(rparams, packet_id, func);
            return true;
        }
        return false;
    }

    void async_pingreq(async_handler_t const& func = async_handler_t()) {
        async_send_pingreq(func);
    }

    void async_connect(
        std::uint16_t keep_alive_sec,
        async_handler_t const& func = async_handler_t()) {
        async_send_connect(keep_alive_sec, func);
    }

    // For broker side
    void async_connack(
        bool session_present,
        std::uint8_t return_code,
        async_handler_t const& func = async_handler_t()) {
        async_send_connack(session_present, return_code, func);
    }

    void async_puback(std::uint16_t packet_id, async_handler_t const& func = async_handler_t()) {
        async_send_puback(packet_id, func);
    }

    void async_pubrec(std::uint16_t packet_id, async_handler_t const& func = async_handler_t()) {
        async_send_pubrec(packet_id, func);
    }

    void async_pubrel(std::uint16_t packet_id, async_handler_t const& func = async_handler_t()) {
        async_send_pubrel(packet_id, func);
    }

    void async_pubcomp(std::uint16_t packet_id, async_handler_t const& func = async_handler_t()) {
        async_send_pubcomp(packet_id, func);
    }

    template <typename... Args>
    typename std::enable_if<
        sizeof...(Args) != 0
    >::type
    async_suback(
        std::uint16_t packet_id,
        std::uint8_t qos, Args&&... args) {
        async_suback_imp(packet_id, qos, std::forward<Args>(args)...);
    }

    void async_suback(
        std::uint16_t packet_id,
        std::uint8_t qos,
        async_handler_t const& func = async_handler_t()) {
        std::vector<std::uint8_t> params;
        async_send_suback(params, packet_id, qos, func);
    }

    void async_suback(
        std::uint16_t packet_id,
        std::vector<std::uint8_t> const& qoss,
        async_handler_t const& func = async_handler_t()) {
        async_send_suback(qoss, packet_id, func);
    }

    void async_unsuback(
        std::uint16_t packet_id,
        async_handler_t const& func = async_handler_t()) {
        async_send_unsuback(packet_id, func);
    }

    void async_pingresp(async_handler_t const& func = async_handler_t()) {
        async_send_pingresp(func);
    }

    bool handle_close_or_error(boost::system::error_code const& ec) {
        if (!ec) return false;
        connected_ = false;
        shutdown(*socket_);
        if (ec == as::error::eof ||
            ec == as::error::connection_reset
#if !defined(MQTT_NO_TLS)
            ||
            ec.value() == ERR_PACK(ERR_LIB_SSL, 0, SSL_R_SHORT_READ)
#endif // defined(MQTT_NO_TLS)
        ) {
            handle_close();
            return true;
        }
        if (ec == as::error::eof ||
            ec == as::error::connection_reset) {
            handle_close();
            return true;
        }
        handle_error(ec);
        return true;
    }

    bool connected() const {
        return connected_;
    }

    void set_connect() {
        connected_ = true;
    }

    void clear_stored_publish(std::uint16_t packet_id) {
        LockGuard<Mutex> lck (store_mtx_);
        auto& idx = store_.template get<tag_packet_id>();
        auto r = idx.equal_range(packet_id);
        idx.erase(std::get<0>(r), std::get<1>(r));
        packet_id_.erase(packet_id);
    }

    std::unique_ptr<Socket>& socket() {
        return socket_;
    }
    std::unique_ptr<Socket> const& socket() const {
        return socket_;
    }

    template <typename F>
    void for_each_store(F f) {
        LockGuard<Mutex> lck (store_mtx_);
        auto& idx = store_.template get<tag_seq>();
        for (auto const & e : idx) {
            f(e.ptr(), e.size());
        }
    }

protected:
    void async_read_control_packet_type(async_handler_t const& func) {
        auto self = this->shared_from_this();
        as::async_read(
            *socket_,
            as::buffer(&buf_, 1),
            [this, self, func](
                boost::system::error_code const& ec,
                std::size_t bytes_transferred){
                if (handle_close_or_error(ec)) {
                    if (func) func(ec);
                    return;
                }
                if (bytes_transferred != 1) {
                    if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
                    return;
                }
                handle_control_packet_type(func);
            }
        );
    }

private:
#if !defined(MQTT_NO_TLS)
    template <typename T>
    typename std::enable_if<
        std::is_same<T, as::ssl::stream<as::ip::tcp::socket>>::value
    >::type
    shutdown(T& socket) {
        boost::system::error_code ec;
        socket.shutdown(ec);
        socket.lowest_layer().close();
    }
#endif // defined(MQTT_NO_TLS)

    template <typename T>
    typename std::enable_if<
        std::is_same<T, as::ip::tcp::socket>::value
    >::type
    shutdown(T& socket) {
        socket.close();
    }

    template <typename... Args>
    typename std::enable_if<
        std::is_convertible<
            typename std::tuple_element<sizeof...(Args) - 1, std::tuple<Args...>>::type,
            async_handler_t
        >::value,
        std::uint16_t
    >::type
    async_subscribe_imp(
        std::string const& topic_name,
        std::uint8_t qos, Args&&... args) {
        std::uint16_t packet_id = acquire_unique_packet_id();
        std::vector<std::tuple<std::reference_wrapper<std::string const>, std::uint8_t>> params;
        params.reserve((sizeof...(args) + 2) / 2);
        async_send_subscribe(params, packet_id, topic_name, qos, std::forward<Args>(args)...);
        return packet_id;
    }

    template <typename... Args>
    typename std::enable_if<
        !std::is_convertible<
            typename std::tuple_element<sizeof...(Args) - 1, std::tuple<Args...>>::type,
            async_handler_t
        >::value,
        std::uint16_t
    >::type
    async_subscribe_imp(
        std::string const& topic_name,
        std::uint8_t qos, Args&&... args) {
        std::uint16_t packet_id = acquire_unique_packet_id();
        std::vector<std::tuple<std::reference_wrapper<std::string const>, std::uint8_t>> params;
        params.reserve((sizeof...(args) + 2) / 2);
        async_send_subscribe(params, packet_id, topic_name, qos, std::forward<Args>(args)..., async_handler_t());
        return packet_id;
    }

    template <typename... Args>
    typename std::enable_if<
        std::is_convertible<
            typename std::tuple_element<sizeof...(Args) - 1, std::tuple<Args...>>::type,
            async_handler_t
        >::value,
        std::uint16_t
    >::type
    async_unsubscribe_imp(
        std::string const& topic_name,
        Args&&... args) {
        std::uint16_t packet_id = acquire_unique_packet_id();
        std::vector<std::reference_wrapper<std::string const>> params;
        params.reserve(sizeof...(args));
        async_send_unsubscribe(params, packet_id, topic_name, std::forward<Args>(args)...);
        return packet_id;
    }

    template <typename... Args>
    typename std::enable_if<
        !std::is_convertible<
            typename std::tuple_element<sizeof...(Args) - 1, std::tuple<Args...>>::type,
            async_handler_t
        >::value,
        std::uint16_t
    >::type
    async_unsubscribe_imp(
        std::string const& topic_name,
        Args&&... args) {
        std::uint16_t packet_id = acquire_unique_packet_id();
        std::vector<std::reference_wrapper<std::string const>> params;
        params.reserve(sizeof...(args) + 1);
        async_send_unsubscribe(params, packet_id, topic_name, std::forward<Args>(args)..., async_handler_t());
        return packet_id;
    }

    template <typename... Args>
    typename std::enable_if<
        std::is_convertible<
            typename std::tuple_element<sizeof...(Args) - 1, std::tuple<Args...>>::type,
            async_handler_t
        >::value,
        bool
    >::type
    async_subscribe_imp(
        std::uint16_t packet_id,
        std::string const& topic_name,
        std::uint8_t qos, Args&&... args) {
        if (register_packet_id(packet_id)) {
            std::vector<std::tuple<std::reference_wrapper<std::string const>, std::uint8_t>> params;
            params.reserve((sizeof...(args) + 2) / 2);
            async_send_subscribe(params, packet_id, topic_name, qos, std::forward<Args>(args)...);
            return true;
        }
        return false;
    }

    template <typename... Args>
    typename std::enable_if<
        !std::is_convertible<
            typename std::tuple_element<sizeof...(Args) - 1, std::tuple<Args...>>::type,
            async_handler_t
        >::value,
        bool
    >::type
    async_subscribe_imp(
        std::uint16_t packet_id,
        std::string const& topic_name,
        std::uint8_t qos, Args&&... args) {
        if (register_packet_id(packet_id)) {
            std::vector<std::tuple<std::reference_wrapper<std::string const>, std::uint8_t>> params;
            params.reserve((sizeof...(args) + 2) / 2);
            async_send_subscribe(params, packet_id, topic_name, qos, std::forward<Args>(args)..., async_handler_t());
            return true;
        }
        return false;
    }

    template <typename... Args>
    typename std::enable_if<
        std::is_convertible<
            typename std::tuple_element<sizeof...(Args) - 1, std::tuple<Args...>>::type,
            async_handler_t
        >::value,
        bool
    >::type
    async_unsubscribe_imp(
        std::uint16_t packet_id,
        std::string const& topic_name,
        Args&&... args) {
        if (register_packet_id(packet_id)) {
            std::vector<std::reference_wrapper<std::string const>> params;
            params.reserve(sizeof...(args));
            async_send_unsubscribe(params, packet_id, topic_name, std::forward<Args>(args)...);
            return true;
        }
        return false;
    }

    template <typename... Args>
    typename std::enable_if<
        !std::is_convertible<
            typename std::tuple_element<sizeof...(Args) - 1, std::tuple<Args...>>::type,
            async_handler_t
        >::value,
        bool
    >::type
    async_unsubscribe_imp(
        std::uint16_t packet_id,
        std::string const& topic_name,
        Args&&... args) {
        if (register_packet_id(packet_id)) {
            std::vector<std::reference_wrapper<std::string const>> params;
            params.reserve(sizeof...(args) + 1);
            async_send_unsubscribe(params, packet_id, topic_name, std::forward<Args>(args)..., async_handler_t());
            return true;
        }
        return false;
    }

    template <typename... Args>
    typename std::enable_if<
        std::is_convertible<
            typename std::tuple_element<sizeof...(Args) - 1, std::tuple<Args...>>::type,
            async_handler_t
        >::value
    >::type
    async_suback_imp(
        std::uint16_t packet_id,
        std::uint8_t qos, Args&&... args) {
        std::vector<std::uint8_t> params;
        async_send_suback(params, packet_id, qos, std::forward<Args>(args)...);
    }

    template <typename... Args>
    typename std::enable_if<
        !std::is_convertible<
            typename std::tuple_element<sizeof...(Args) - 1, std::tuple<Args...>>::type,
            async_handler_t
        >::value
    >::type
    async_suback_imp(
        std::uint16_t packet_id,
        std::uint8_t qos, Args&&... args) {
        std::vector<std::uint8_t> params;
        async_send_suback(params, packet_id, qos, std::forward<Args>(args)..., async_handler_t());
    }

    class send_buffer {
    public:
        send_buffer():buf_(std::make_shared<std::string>(static_cast<int>(payload_position_), 0)) {}

        std::shared_ptr<std::string> const& buf() const {
            return buf_;
        }

        std::shared_ptr<std::string>& buf() {
            return buf_;
        }

        std::tuple<char*, std::size_t> finalize(std::uint8_t fixed_header) {
            auto rb = remaining_bytes(buf_->size() - payload_position_);
            std::size_t start_position = payload_position_ - rb.size() - 1;
            (*buf_)[start_position] = fixed_header;
            buf_->replace(start_position + 1, rb.size(), rb);
            return std::make_tuple(
                &(*buf_)[start_position],
                buf_->size() - start_position);
        }
    private:
        static constexpr std::size_t const payload_position_ = 5;
        std::shared_ptr<std::string> buf_;
    };

    class packet {
    public:
        packet(
            std::shared_ptr<std::string> const& b = nullptr,
            char* p = nullptr,
            std::size_t s = 0)
            :
            buf_(b),
            ptr_(p),
            size_(s) {}
        std::shared_ptr<std::string> const& buf() const { return buf_; }
        char const* ptr() const { return ptr_; }
        char* ptr() { return ptr_; }
        std::size_t size() const { return size_; }
    private:
        std::shared_ptr<std::string> buf_;
        char* ptr_;
        std::size_t size_;
    };

    struct store {
        store(
            std::uint16_t id,
            std::uint8_t type,
            std::shared_ptr<std::string> const& b = nullptr,
            char* p = nullptr,
            std::size_t s = 0)
            :
            packet_id_(id),
            expected_control_packet_type_(type),
            packet_(b, p, s) {}
        std::uint16_t packet_id() const { return packet_id_; }
        std::uint8_t expected_control_packet_type() const { return expected_control_packet_type_; }
        std::shared_ptr<std::string> const& buf() const { return packet_.buf(); }
        char const* ptr() const { return packet_.ptr(); }
        char* ptr() { return packet_.ptr(); }
        std::size_t size() const { return packet_.size(); }
    private:
        std::uint16_t packet_id_;
        std::uint8_t expected_control_packet_type_;
        packet packet_;
    };

    struct tag_packet_id {};
    struct tag_packet_id_type {};
    struct tag_seq {};
    using mi_store = mi::multi_index_container<
        store,
        mi::indexed_by<
            mi::ordered_unique<
                mi::tag<tag_packet_id_type>,
                mi::composite_key<
                    store,
                    mi::const_mem_fun<
                        store, std::uint16_t,
                        &store::packet_id
                    >,
                    mi::const_mem_fun<
                        store, std::uint8_t,
                        &store::expected_control_packet_type
                    >
                >
            >,
            mi::ordered_non_unique<
                mi::tag<tag_packet_id>,
                mi::const_mem_fun<
                    store, std::uint16_t,
                    &store::packet_id
                >
            >,
            mi::sequenced<
                mi::tag<tag_seq>
            >
        >
    >;

    void handle_control_packet_type(async_handler_t const& func) {
        fixed_header_ = static_cast<std::uint8_t>(buf_);
        remaining_length_ = 0;
        remaining_length_multiplier_ = 1;
        auto self = this->shared_from_this();
        as::async_read(
            *socket_,
            as::buffer(&buf_, 1),
            [this, self, func](
                boost::system::error_code const& ec,
                std::size_t bytes_transferred){
                if (handle_close_or_error(ec)) {
                    if (func) func(ec);
                    return;
                }
                if (bytes_transferred != 1) {
                    if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
                    return;
                }
                handle_remaining_length(func);
            }
        );
    }

    void handle_remaining_length(async_handler_t const& func) {
        remaining_length_ += (buf_ & 0b01111111) * remaining_length_multiplier_;
        remaining_length_multiplier_ *= 128;
        if (remaining_length_multiplier_ > 128 * 128 * 128 * 128) throw remaining_length_error();
        auto self = this->shared_from_this();
        if (buf_ & 0b10000000) {
            as::async_read(
                *socket_,
                as::buffer(&buf_, 1),
                [this, self, func](
                    boost::system::error_code const& ec,
                    std::size_t bytes_transferred){
                    if (handle_close_or_error(ec)) {
                        if (func) func(ec);
                        return;
                    }
                    if (bytes_transferred != 1) {
                        if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
                        return;
                    }
                    handle_remaining_length(func);
                }
            );
        }
        else {
            payload_.resize(remaining_length_);
            if (remaining_length_ == 0) {
                handle_payload(func);
                return;
            }
            as::async_read(
                *socket_,
                as::buffer(payload_),
                [this, self, func](
                    boost::system::error_code const& ec,
                    std::size_t bytes_transferred){
                    if (handle_close_or_error(ec)) {
                        if (func) func(ec);
                        return;
                    }
                    if (bytes_transferred != remaining_length_) {
                        if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
                        return;
                    }
                    handle_payload(func);
                }
            );
        }
    }

    void handle_payload(async_handler_t const& func) {
        auto control_packet_type = get_control_packet_type(fixed_header_);
        bool ret = false;
        switch (control_packet_type) {
        case control_packet_type::connect:
            ret = handle_connect(func);
            break;
        case control_packet_type::connack:
            ret = handle_connack(func);
            break;
        case control_packet_type::publish:
            ret = handle_publish(func);
            break;
        case control_packet_type::puback:
            ret = handle_puback(func);
            break;
        case control_packet_type::pubrec:
            ret = handle_pubrec(func);
            break;
        case control_packet_type::pubrel:
            ret = handle_pubrel(func);
            break;
        case control_packet_type::pubcomp:
            ret = handle_pubcomp(func);
            break;
        case control_packet_type::subscribe:
            ret = handle_subscribe(func);
            break;
        case control_packet_type::suback:
            ret = handle_suback(func);
            break;
        case control_packet_type::unsubscribe:
            ret = handle_unsubscribe(func);
            break;
        case control_packet_type::unsuback:
            ret = handle_unsuback(func);
            break;
        case control_packet_type::pingreq:
            ret = handle_pingreq(func);
            break;
        case control_packet_type::pingresp:
            ret = handle_pingresp(func);
            break;
        case control_packet_type::disconnect:
            handle_disconnect(func);
            ret = false;
            break;
        default:
            break;
        }
        if (ret) async_read_control_packet_type(func);
        else if (func) func(boost::system::errc::make_error_code(boost::system::errc::success));
    }

    void handle_close() {
        if (h_close_) h_close_();
    }

    void handle_error(boost::system::error_code const& ec) {
        if (h_error_) h_error_(ec);
    }

    bool handle_connect(async_handler_t const& func) {
        std::size_t i = 0;
        if (remaining_length_ < 10 ||
            payload_[i++] != 0x00 ||
            payload_[i++] != 0x04 ||
            payload_[i++] != 'M' ||
            payload_[i++] != 'Q' ||
            payload_[i++] != 'T' ||
            payload_[i++] != 'T' ||
            payload_[i++] != 0x04) {
            if (func) func(boost::system::errc::make_error_code(boost::system::errc::protocol_error));
            return false;
        }
        char byte8 = payload_[i++];

        std::uint16_t keep_alive;
        keep_alive = make_uint16_t(payload_[i], payload_[i + 1]);
        i += 2;

        if (remaining_length_ < i + 2) {
            if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
            return false;
        }

        std::uint16_t client_id_length;
        client_id_length = make_uint16_t(payload_[i], payload_[i + 1]);
        i += 2;
        if (remaining_length_ < i + client_id_length) {
            if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
            return false;
        }
        std::string client_id(payload_.data() + i, client_id_length);
        i += client_id_length;

        bool clean_session = connect_flags::has_clean_session(byte8);
        boost::optional<will> w;
        if (connect_flags::has_will_flag(byte8)) {
            std::uint16_t topic_name_length;
            topic_name_length = make_uint16_t(payload_[i], payload_[i + 1]);
            i += 2;
            if (remaining_length_ < i + topic_name_length) {
                if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
                return false;
            }
            std::string topic_name(payload_.data() + i, topic_name_length);
            i += topic_name_length;
            std::uint16_t will_message_length;
            will_message_length = make_uint16_t(payload_[i], payload_[i + 1]);
            i += 2;
            if (remaining_length_ < i + will_message_length) {
                if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
                return false;
            }
            std::string will_message(payload_.data() + i, topic_name_length);
            i += will_message_length;
            w = will(topic_name,
                        will_message,
                        connect_flags::has_will_retain(byte8),
                        connect_flags::will_qos(byte8));
        }
        boost::optional<std::string> user_name;
        if (connect_flags::has_user_name_flag(byte8)) {
            std::uint16_t user_name_length;
            user_name_length = make_uint16_t(payload_[i], payload_[i + 1]);
            i += 2;
            if (remaining_length_ < i + user_name_length) {
                if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
                return false;
            }
            user_name = std::string(payload_.data() + i, user_name_length);
            i += user_name_length;
        }
        boost::optional<std::string> password;
        if (connect_flags::has_password_flag(byte8)) {
            std::uint16_t password_length;
            password_length = make_uint16_t(payload_[i], payload_[i + 1]);
            i += 2;
            if (remaining_length_ < i + password_length) {
                if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
                return false;
            }
            password = std::string(payload_.data() + i, password_length);
            i += password_length;
        }
        if (h_connect_) {
            if (h_connect_(client_id, user_name, password, std::move(w), clean_session, keep_alive)) {
                return true;
            }
            return false;
        }
        return true;
    }

    bool handle_connack(async_handler_t const& func) {
        if (remaining_length_ != 2) {
            if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
            return false;
        }
        if (static_cast<std::uint8_t>(payload_[1]) == connect_return_code::accepted) {
            if (clean_session_) {
                LockGuard<Mutex> lck (store_mtx_);
                store_.clear();
            }
            else {
                LockGuard<Mutex> lck (store_mtx_);
                auto& idx = store_.template get<tag_seq>();
                auto it = idx.begin();
                auto end = idx.end();
                while (it != end) {
                    if (it->buf()) {
                        idx.modify(
                            it,
                            [this](store& e){
                                if (e.expected_control_packet_type() == control_packet_type::puback ||
                                    e.expected_control_packet_type() == control_packet_type::pubrec) {
                                    *e.ptr() |= 0b00001000; // set DUP flag
                                }
                                // I choose sync write intentionaly.
                                // If calling async_write, and then disconnected,
                                // strand object would be dangling references.
                                this->write(e.ptr(), e.size());
                            }
                        );
                        ++it;
                    }
                    else {
                        it = idx.erase(it);
                    }
                }
            }
        }
        bool session_present = is_session_present(payload_[0]);
        if (h_connack_) return h_connack_(session_present, static_cast<std::uint8_t>(payload_[1]));
        return true;
    }

    template <typename F, typename AF>
    void auto_pub_response(F const& f, AF const& af) {
        if (auto_pub_response_) {
            if (auto_pub_response_async_) af();
            else f();
        }
    }

    bool handle_publish(async_handler_t const& func) {
        if (remaining_length_ < 2) {
            if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
            return false;
        }
        std::size_t i = 0;
        std::uint16_t topic_name_length = make_uint16_t(payload_[i], payload_[i + 1]);
        i += 2;
        if (remaining_length_ < i + topic_name_length) {
            if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
            return false;
        }
        std::string topic_name(payload_.data() + i, topic_name_length);
        i += topic_name_length;
        boost::optional<std::uint16_t> packet_id;
        auto qos = publish::get_qos(fixed_header_);
        switch (qos) {
        case qos::at_most_once:
            if (h_publish_) {
                std::string contents(payload_.data() + i, payload_.size() - i);
                return h_publish_(fixed_header_, packet_id, std::move(topic_name), std::move(contents));
            }
            break;
        case qos::at_least_once: {
            if (remaining_length_ < i + 2) {
                if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
                return false;
            }
            packet_id = make_uint16_t(payload_[i], payload_[i + 1]);
            i += 2;
            auto res = [this, &packet_id, &func] {
                auto_pub_response(
                    [this, &packet_id] {
                        if (connected_) send_puback(*packet_id);
                    },
                    [this, &packet_id, &func] {
                        if (connected_) async_send_puback(*packet_id, func);
                    }
                );
            };
            if (h_publish_) {
                std::string contents(payload_.data() + i, payload_.size() - i);
                if (h_publish_(fixed_header_, packet_id, std::move(topic_name), std::move(contents))) {
                    res();
                    return true;
                }
                return false;
            }
            res();
        } break;
        case qos::exactly_once: {
            if (remaining_length_ < i + 2) {
                if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
                return false;
            }
            packet_id = make_uint16_t(payload_[i], payload_[i + 1]);
            i += 2;
            auto res = [this, &packet_id, &func] {
                auto_pub_response(
                    [this, &packet_id] {
                        if (connected_) send_pubrec(*packet_id);
                    },
                    [this, &packet_id, &func] {
                        if (connected_) async_send_pubrec(*packet_id, func);
                    }
                );
            };
            auto it = qos2_publish_handled_.find(*packet_id);
            if (it == qos2_publish_handled_.end()) {
                std::string contents(payload_.data() + i, payload_.size() - i);
                if (h_publish_) {
                    std::string contents(payload_.data() + i, payload_.size() - i);
                    if (h_publish_(fixed_header_, packet_id, std::move(topic_name), std::move(contents))) {
                        res();
                        return true;
                    }
                    return false;
                }
            }
            res();
        } break;
        default:
            break;
        }
        return true;
    }

    bool handle_puback(async_handler_t const& func) {
        if (remaining_length_ != 2) {
            if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
            return false;
        }
        std::uint16_t packet_id = make_uint16_t(payload_[0], payload_[1]);
        {
            LockGuard<Mutex> lck (store_mtx_);
            auto& idx = store_.template get<tag_packet_id_type>();
            auto r = idx.equal_range(std::make_tuple(packet_id, control_packet_type::puback));
            idx.erase(std::get<0>(r), std::get<1>(r));
            packet_id_.erase(packet_id);
        }
        if (h_puback_) return h_puback_(packet_id);
        return true;
    }

    bool handle_pubrec(async_handler_t const& func) {
        if (remaining_length_ != 2) {
            if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
            return false;
        }
        std::uint16_t packet_id = make_uint16_t(payload_[0], payload_[1]);
        {
            LockGuard<Mutex> lck (store_mtx_);
            auto& idx = store_.template get<tag_packet_id_type>();
            auto r = idx.equal_range(std::make_tuple(packet_id, control_packet_type::pubrec));
            idx.erase(std::get<0>(r), std::get<1>(r));
            // packet_id shouldn't be erased here.
            // It is reused for pubrel/pubcomp.
        }
        auto res = [this, &packet_id, &func] {
            auto_pub_response(
                [this, &packet_id] {
                    if (connected_) send_pubrel(packet_id);
                    else store_pubrel(packet_id);
                },
                [this, &packet_id, &func] {
                    if (connected_) async_send_pubrel(packet_id, func);
                    else store_pubrel(packet_id);
                }
            );
        };
        if (h_pubrec_) {
            if (h_pubrec_(packet_id)) {
                res();
                return true;
            }
            return false;
        }
        res();
        return true;
    }

    bool handle_pubrel(async_handler_t const& func) {
        if (remaining_length_ != 2) {
            if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
            return false;
        }
        std::uint16_t packet_id = make_uint16_t(payload_[0], payload_[1]);
        auto res = [this, &packet_id, &func] {
            auto_pub_response(
                [this, &packet_id] {
                    if (connected_) send_pubcomp(packet_id);
                },
                [this, &packet_id, &func] {
                    if (connected_) async_send_pubcomp(packet_id, func);
                }
            );
        };
        qos2_publish_handled_.erase(packet_id);
        if (h_pubrel_) {
            if (h_pubrel_(packet_id)) {
                res();
                return true;
            }
            return false;
        }
        res();
        return true;
    }

    bool handle_pubcomp(async_handler_t const& func) {
        if (remaining_length_ != 2) {
            if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
            return false;
        }
        std::uint16_t packet_id = make_uint16_t(payload_[0], payload_[1]);
        {
            LockGuard<Mutex> lck (store_mtx_);
            auto& idx = store_.template get<tag_packet_id_type>();
            auto r = idx.equal_range(std::make_tuple(packet_id, control_packet_type::pubcomp));
            idx.erase(std::get<0>(r), std::get<1>(r));
            packet_id_.erase(packet_id);
        }
        if (h_pubcomp_) return h_pubcomp_(packet_id);
        return true;
    }

    bool handle_subscribe(async_handler_t const& func) {
        std::size_t i = 0;
        if (remaining_length_ < 2) {
            if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
            return false;
        }
        std::uint16_t packet_id = make_uint16_t(payload_[i], payload_[i + 1]);
        i += 2;
        std::vector<std::tuple<std::string, std::uint8_t>> entries;
        while (i < remaining_length_) {
            if (remaining_length_ < i + 2) {
                if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
                return false;
            }
            std::uint16_t topic_length = make_uint16_t(payload_[i], payload_[i + 1]);
            i += 2;
            if (remaining_length_ < i + topic_length) {
                if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
                return false;
            }
            std::string topic_filter(payload_.data() + i, topic_length);
            i += topic_length;

            std::uint8_t qos = payload_[i] & 0b00000011;
            ++i;
            entries.emplace_back(std::move(topic_filter), qos);
        }
        if (h_subscribe_) return h_subscribe_(packet_id, std::move(entries));
        return true;
    }

    bool handle_suback(async_handler_t const& func) {
        if (remaining_length_ < 2) {
            if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
            return false;
        }
        std::uint16_t packet_id = make_uint16_t(payload_[0], payload_[1]);
        {
            LockGuard<Mutex> lck (store_mtx_);
            packet_id_.erase(packet_id);
        }
        std::vector<boost::optional<std::uint8_t>> results;
        results.reserve(payload_.size() - 2);
        auto it = payload_.cbegin() + 2;
        auto end = payload_.cend();
        for (; it != end; ++it) {
            if (*it & 0b10000000) {
                results.push_back(boost::none);
            }
            else {
                results.push_back(*it);
            }
        }
        if (h_suback_) return h_suback_(packet_id, std::move(results));
        return true;
    }

    bool handle_unsubscribe(async_handler_t const& func) {
        std::size_t i = 0;
        if (remaining_length_ < 2) {
            if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
            return false;
        }
        std::uint16_t packet_id = make_uint16_t(payload_[i], payload_[i + 1]);
        i += 2;
        std::vector<std::string> topic_filters;
        while (i < remaining_length_) {
            if (remaining_length_ < i + 2) {
                if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
                return false;
            }
            std::uint16_t topic_length = make_uint16_t(payload_[i], payload_[i + 1]);
            i += 2;
            if (remaining_length_ < i + topic_length) {
                if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
                return false;
            }
            std::string topic_filter(payload_.data() + i, topic_length);
            i += topic_length;

            topic_filters.emplace_back(std::move(topic_filter));
        }
        if (h_unsubscribe_) return h_unsubscribe_(packet_id, std::move(topic_filters));
        return true;
    }

    bool handle_unsuback(async_handler_t const& func) {
        if (remaining_length_ != 2) {
            if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
            return false;
        }
        std::uint16_t packet_id = make_uint16_t(payload_[0], payload_[1]);
        {
            LockGuard<Mutex> lck (store_mtx_);
            packet_id_.erase(packet_id);
        }
        if (h_unsuback_) return h_unsuback_(packet_id);
        return true;
    }

    bool handle_pingreq(async_handler_t const& func) {
        if (remaining_length_ != 0) {
            if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
            return false;
        }
        if (h_pingreq_) return h_pingreq_();
        return true;
    }

    bool handle_pingresp(async_handler_t const& func) {
        if (remaining_length_ != 0) {
            if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
            return false;
        }
        if (h_pingresp_) return h_pingresp_();
        return true;
    }

    void handle_disconnect(async_handler_t const& func) {
        if (remaining_length_ != 0) {
            if (func) func(boost::system::errc::make_error_code(boost::system::errc::message_size));
            return;
        }
        if (h_disconnect_) h_disconnect_();
    }

    // Blocking senders.
    void send_connect(std::uint16_t keep_alive_sec) {

        send_buffer sb;
        std::size_t payload_position = 5; // Fixed Header + max size of Remaining bytes
        sb.buf()->resize(payload_position);
        sb.buf()->push_back(0x00);   // Length MSB(0)
        sb.buf()->push_back(0x04);   // Length LSB(4)
        sb.buf()->push_back('M');
        sb.buf()->push_back('Q');
        sb.buf()->push_back('T');
        sb.buf()->push_back('T');
        sb.buf()->push_back(0x04);   // Level(4) MQTT version 3.1.1
        std::size_t connect_flags_position = sb.buf()->size();
        char initial_value = clean_session_ ? 0b00000010 : 0;
        sb.buf()->push_back(initial_value); // Connect Flags
        // Keep Alive MSB
        sb.buf()->push_back(static_cast<std::uint8_t>(keep_alive_sec >> 8));
        // Keep Alive LSB
        sb.buf()->push_back(static_cast<std::uint8_t>(keep_alive_sec & 0xff));

        // endpoint id
        if (!utf8string::is_valid_length(client_id_)) throw utf8string_length_error();
        if (!utf8string::is_valid_contents(client_id_)) throw utf8string_contents_error();
        sb.buf()->insert(sb.buf()->size(), encoded_length(client_id_));
        sb.buf()->insert(sb.buf()->size(), client_id_);

        // will
        if (will_) {
            char& c = (*sb.buf())[connect_flags_position];
            c |= connect_flags::will_flag;
            if (will_->retain()) c |= connect_flags::will_retain;
            connect_flags::set_will_qos(c, will_->qos());

            if (!utf8string::is_valid_length(will_->topic())) throw utf8string_length_error();
            if (!utf8string::is_valid_contents(will_->topic())) throw utf8string_contents_error();
            sb.buf()->insert(sb.buf()->size(), encoded_length(will_->topic()));
            sb.buf()->insert(sb.buf()->size(), will_->topic());

            if (will_->message().size() > 0xffff) throw will_message_length_error();
            sb.buf()->insert(sb.buf()->size(), encoded_length(will_->message()));
            sb.buf()->insert(sb.buf()->size(), will_->message());
        }

        // user_name, password
        if (user_name_) {
            char& c = (*sb.buf())[connect_flags_position];
            c |= connect_flags::user_name_flag;
            std::string const& str = *user_name_;
            if (!utf8string::is_valid_length(str)) throw utf8string_length_error();
            if (!utf8string::is_valid_contents(str)) throw utf8string_contents_error();
            sb.buf()->insert(sb.buf()->size(), encoded_length(str));
            sb.buf()->insert(sb.buf()->size(), str);
        }
        if (password_) {
            char& c = (*sb.buf())[connect_flags_position];
            c |= connect_flags::password_flag;
            std::string const& str = *password_;
            if (str.size() > 0xffff) throw password_length_error();
            sb.buf()->insert(sb.buf()->size(), encoded_length(str));
            sb.buf()->insert(sb.buf()->size(), str);
        }

        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::connect, 0));
        write(std::get<0>(ptr_size), std::get<1>(ptr_size));
    }

    void send_connack(bool session_present, std::uint8_t return_code) {
        send_buffer sb;
        sb.buf()->push_back(static_cast<char>(session_present ? 1 : 0));
        sb.buf()->push_back(static_cast<char>(return_code));
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::connack, 0b0000));
        write(std::get<0>(ptr_size), std::get<1>(ptr_size));
    }

    void send_publish(
        std::string const& topic_name,
        std::uint16_t qos,
        bool retain,
        bool dup,
        std::uint16_t packet_id,
        std::string const& payload) {

        send_buffer sb;
        if (!utf8string::is_valid_length(topic_name)) throw utf8string_length_error();
        if (!utf8string::is_valid_contents(topic_name)) throw utf8string_contents_error();
        sb.buf()->insert(sb.buf()->size(), encoded_length(topic_name));
        sb.buf()->insert(sb.buf()->size(), topic_name);
        if (qos == qos::at_least_once ||
            qos == qos::exactly_once) {
            sb.buf()->push_back(static_cast<char>(packet_id >> 8));
            sb.buf()->push_back(static_cast<char>(packet_id & 0xff));
        }
        sb.buf()->insert(sb.buf()->size(), payload);
        std::uint8_t flags = 0;
        if (retain) flags |= 0b00000001;
        if (dup) flags |= 0b00001000;
        flags |= qos << 1;
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::publish, flags));
        write(std::get<0>(ptr_size), std::get<1>(ptr_size));
        if (qos > 0) {
            flags |= 0b00001000;
            ptr_size = sb.finalize(make_fixed_header(control_packet_type::publish, flags));
            LockGuard<Mutex> lck (store_mtx_);
            store_.emplace(
                packet_id,
                qos == qos::at_least_once ? control_packet_type::puback
                                          : control_packet_type::pubrec,
                sb.buf(),
                std::get<0>(ptr_size),
                std::get<1>(ptr_size));
        }
    }

    void send_puback(std::uint16_t packet_id) {
        send_buffer sb;
        sb.buf()->push_back(static_cast<char>(packet_id >> 8));
        sb.buf()->push_back(static_cast<char>(packet_id & 0xff));
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::puback, 0b0000));
        write(std::get<0>(ptr_size), std::get<1>(ptr_size));
        if (h_pub_res_sent_) h_pub_res_sent_(packet_id);
    }

    void send_pubrec(std::uint16_t packet_id) {
        send_buffer sb;
        sb.buf()->push_back(static_cast<char>(packet_id >> 8));
        sb.buf()->push_back(static_cast<char>(packet_id & 0xff));
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::pubrec, 0b0000));
        write(std::get<0>(ptr_size), std::get<1>(ptr_size));
    }

    void send_pubrel(std::uint16_t packet_id) {
        send_buffer sb;
        sb.buf()->push_back(static_cast<char>(packet_id >> 8));
        sb.buf()->push_back(static_cast<char>(packet_id & 0xff));
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::pubrel, 0b0010));
        write(std::get<0>(ptr_size), std::get<1>(ptr_size));
        LockGuard<Mutex> lck (store_mtx_);
        store_.emplace(
            packet_id,
            control_packet_type::pubcomp,
            sb.buf(),
            std::get<0>(ptr_size),
            std::get<1>(ptr_size));
    }

    void store_pubrel(std::uint16_t packet_id) {
        send_buffer sb;
        sb.buf()->push_back(static_cast<char>(packet_id >> 8));
        sb.buf()->push_back(static_cast<char>(packet_id & 0xff));
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::pubrel, 0b0010));
        LockGuard<Mutex> lck (store_mtx_);
        store_.emplace(
            packet_id,
            control_packet_type::pubcomp,
            sb.buf(),
            std::get<0>(ptr_size),
            std::get<1>(ptr_size));
    }

    void send_pubcomp(std::uint16_t packet_id) {
        send_buffer sb;
        sb.buf()->push_back(static_cast<char>(packet_id >> 8));
        sb.buf()->push_back(static_cast<char>(packet_id & 0xff));
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::pubcomp, 0b0000));
        write(std::get<0>(ptr_size), std::get<1>(ptr_size));
        if (h_pub_res_sent_) h_pub_res_sent_(packet_id);
    }

    template <typename... Args>
    void send_subscribe(
        std::vector<std::tuple<std::reference_wrapper<std::string const>, std::uint8_t>>& params,
        std::uint16_t packet_id,
        std::string const& topic_name,
        std::uint8_t qos, Args... args) {
        params.push_back(std::make_tuple(std::cref(topic_name), qos));
        send_subscribe(params, packet_id, args...);
    }

    void send_subscribe(
        std::vector<std::tuple<std::reference_wrapper<std::string const>, std::uint8_t>>& params,
        std::uint16_t packet_id) {
        send_buffer sb;
        sb.buf()->push_back(static_cast<char>(packet_id >> 8));
        sb.buf()->push_back(static_cast<char>(packet_id & 0xff));
        for (auto const& e : params) {
            if (!utf8string::is_valid_length(std::get<0>(e))) throw utf8string_length_error();
            if (!utf8string::is_valid_contents(std::get<0>(e))) throw utf8string_contents_error();
            sb.buf()->insert(sb.buf()->size(), encoded_length(std::get<0>(e)));
            sb.buf()->insert(sb.buf()->size(), std::get<0>(e));
            sb.buf()->push_back(std::get<1>(e));
        }
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::subscribe, 0b0010));
        write(std::get<0>(ptr_size), std::get<1>(ptr_size));
    }

    template <typename... Args>
    void send_suback(
        std::vector<std::uint8_t>& params,
        std::uint16_t packet_id,
        std::uint8_t qos, Args&&... args) {
        params.push_back(qos);
        send_suback(params, packet_id, std::forward<Args>(args)...);
    }

    void send_suback(
        std::vector<std::uint8_t> const& params,
        std::uint16_t packet_id) {
        send_buffer sb;
        sb.buf()->push_back(static_cast<char>(packet_id >> 8));
        sb.buf()->push_back(static_cast<char>(packet_id & 0xff));
        for (auto const& e : params) {
            sb.buf()->push_back(e);
        }
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::suback, 0b0000));
        write(std::get<0>(ptr_size), std::get<1>(ptr_size));
    }

    template <typename... Args>
    void send_unsubscribe(
        std::vector<std::reference_wrapper<std::string const>>& params,
        std::uint16_t packet_id,
        std::string const& topic_name,
        Args... args) {
        params.push_back(std::cref(topic_name));
        send_unsubscribe(params, packet_id, args...);
    }

    void send_unsubscribe(
        std::vector<std::reference_wrapper<std::string const>>& params,
        std::uint16_t packet_id) {
        send_buffer sb;
        sb.buf()->push_back(static_cast<char>(packet_id >> 8));
        sb.buf()->push_back(static_cast<char>(packet_id & 0xff));
        for (auto const& e : params) {
            if (!utf8string::is_valid_length(e)) throw utf8string_length_error();
            if (!utf8string::is_valid_contents(e)) throw utf8string_contents_error();
            sb.buf()->insert(sb.buf()->size(), encoded_length(e));
            sb.buf()->insert(sb.buf()->size(), e);
        }
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::unsubscribe, 0b0010));
        write(std::get<0>(ptr_size), std::get<1>(ptr_size));
    }

    void send_unsuback(
        std::uint16_t packet_id) {
        send_buffer sb;
        sb.buf()->push_back(static_cast<char>(packet_id >> 8));
        sb.buf()->push_back(static_cast<char>(packet_id & 0xff));
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::unsuback, 0b0010));
        write(std::get<0>(ptr_size), std::get<1>(ptr_size));
    }

    void send_pingreq() {
        send_buffer sb;
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::pingreq, 0b0000));
        write(std::get<0>(ptr_size), std::get<1>(ptr_size));
    }

    void send_pingresp() {
        send_buffer sb;
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::pingresp, 0b0000));
        write(std::get<0>(ptr_size), std::get<1>(ptr_size));
    }
    void send_disconnect() {
        send_buffer sb;
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::disconnect, 0b0000));
        write(std::get<0>(ptr_size), std::get<1>(ptr_size));
    }

    // Blocking write
    void write(char* ptr, std::size_t size) {
        boost::system::error_code ec;
        as::write(*socket_, as::buffer(ptr, size), ec);
        if (ec) handle_error(ec);
    }

    // Non blocking (async) senders
    template <typename F>
    void async_send_connect(std::uint16_t keep_alive_sec, F const& func) {

        send_buffer sb;
        std::size_t payload_position = 5; // Fixed Header + max size of Remaining bytes
        sb.buf()->resize(payload_position);
        sb.buf()->push_back(0x00);   // Length MSB(0)
        sb.buf()->push_back(0x04);   // Length LSB(4)
        sb.buf()->push_back('M');
        sb.buf()->push_back('Q');
        sb.buf()->push_back('T');
        sb.buf()->push_back('T');
        sb.buf()->push_back(0x04);   // Level(4) MQTT version 3.1.1
        std::size_t connect_flags_position = sb.buf()->size();
        char initial_value = clean_session_ ? 0b00000010 : 0;
        sb.buf()->push_back(initial_value); // Connect Flags
        // Keep Alive MSB
        sb.buf()->push_back(static_cast<std::uint8_t>(keep_alive_sec >> 8));
        // Keep Alive LSB
        sb.buf()->push_back(static_cast<std::uint8_t>(keep_alive_sec & 0xff));

        // endpoint id
        if (!utf8string::is_valid_length(client_id_)) throw utf8string_length_error();
        if (!utf8string::is_valid_contents(client_id_)) throw utf8string_contents_error();
        sb.buf()->insert(sb.buf()->size(), encoded_length(client_id_));
        sb.buf()->insert(sb.buf()->size(), client_id_);

        // will
        if (will_) {
            char& c = (*sb.buf())[connect_flags_position];
            c |= connect_flags::will_flag;
            if (will_->retain()) c |= connect_flags::will_retain;
            connect_flags::set_will_qos(c, will_->qos());

            if (!utf8string::is_valid_length(will_->topic())) throw utf8string_length_error();
            if (!utf8string::is_valid_contents(will_->topic())) throw utf8string_contents_error();
            sb.buf()->insert(sb.buf()->size(), encoded_length(will_->topic()));
            sb.buf()->insert(sb.buf()->size(), will_->topic());

            if (will_->message().size() > 0xffff) throw will_message_length_error();
            sb.buf()->insert(sb.buf()->size(), encoded_length(will_->message()));
            sb.buf()->insert(sb.buf()->size(), will_->message());
        }

        // user_name, password
        if (user_name_) {
            char& c = (*sb.buf())[connect_flags_position];
            c |= connect_flags::user_name_flag;
            std::string const& str = *user_name_;
            if (!utf8string::is_valid_length(str)) throw utf8string_length_error();
            if (!utf8string::is_valid_contents(str)) throw utf8string_contents_error();
            sb.buf()->insert(sb.buf()->size(), encoded_length(str));
            sb.buf()->insert(sb.buf()->size(), str);
        }
        if (password_) {
            char& c = (*sb.buf())[connect_flags_position];
            c |= connect_flags::password_flag;
            std::string const& str = *password_;
            if (str.size() > 0xffff) throw password_length_error();
            sb.buf()->insert(sb.buf()->size(), encoded_length(str));
            sb.buf()->insert(sb.buf()->size(), str);
        }

        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::connect, 0));
        async_write(sb.buf(), std::get<0>(ptr_size), std::get<1>(ptr_size), func);
    }

    template <typename F>
    void async_send_connack(bool session_present, std::uint8_t return_code, F const& func) {
        send_buffer sb;
        sb.buf()->push_back(static_cast<char>(session_present ? 1 : 0));
        sb.buf()->push_back(static_cast<char>(return_code));
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::connack, 0b0000));
        async_write(sb.buf(), std::get<0>(ptr_size), std::get<1>(ptr_size), func);
    }

    template <typename F>
    void async_send_publish(
        std::string const& topic_name,
        std::uint16_t qos,
        bool retain,
        bool dup,
        std::uint16_t packet_id,
        std::string const& payload,
        F const& func) {

        send_buffer sb;
        if (!utf8string::is_valid_length(topic_name)) throw utf8string_length_error();
        if (!utf8string::is_valid_contents(topic_name)) throw utf8string_contents_error();
        sb.buf()->insert(sb.buf()->size(), encoded_length(topic_name));
        sb.buf()->insert(sb.buf()->size(), topic_name);
        if (qos == qos::at_least_once ||
            qos == qos::exactly_once) {
            sb.buf()->push_back(static_cast<char>(packet_id >> 8));
            sb.buf()->push_back(static_cast<char>(packet_id & 0xff));
        }
        sb.buf()->insert(sb.buf()->size(), payload);
        std::uint8_t flags = 0;
        if (retain) flags |= 0b00000001;
        if (dup) flags |= 0b00001000;
        flags |= qos << 1;
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::publish, flags));
        async_write(sb.buf(), std::get<0>(ptr_size), std::get<1>(ptr_size), func);
        if (qos > 0) {
            LockGuard<Mutex> lck (store_mtx_);
            store_.emplace(
                packet_id,
                qos == qos::at_least_once ? control_packet_type::puback
                                          : control_packet_type::pubrec,
                sb.buf(),
                std::get<0>(ptr_size),
                std::get<1>(ptr_size));
        }
    }

    template <typename F>
    void async_send_puback(std::uint16_t packet_id, F const& func) {
        send_buffer sb;
        sb.buf()->push_back(static_cast<char>(packet_id >> 8));
        sb.buf()->push_back(static_cast<char>(packet_id & 0xff));
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::puback, 0b0000));
        auto self = this->shared_from_this();
        async_write(
            sb.buf(), std::get<0>(ptr_size), std::get<1>(ptr_size),
            [this, self, packet_id, func](boost::system::error_code const& ec){
                if (func) func(ec);
                if (h_pub_res_sent_) h_pub_res_sent_(packet_id);
            });
    }

    template <typename F>
    void async_send_pubrec(std::uint16_t packet_id, F const& func) {
        send_buffer sb;
        sb.buf()->push_back(static_cast<char>(packet_id >> 8));
        sb.buf()->push_back(static_cast<char>(packet_id & 0xff));
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::pubrec, 0b0000));
        async_write(sb.buf(), std::get<0>(ptr_size), std::get<1>(ptr_size), func);
    }

    template <typename F>
    void async_send_pubrel(std::uint16_t packet_id, F const& func) {
        send_buffer sb;
        sb.buf()->push_back(static_cast<char>(packet_id >> 8));
        sb.buf()->push_back(static_cast<char>(packet_id & 0xff));
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::pubrel, 0b0010));
        async_write(sb.buf(), std::get<0>(ptr_size), std::get<1>(ptr_size), func);
        LockGuard<Mutex> lck (store_mtx_);
        store_.emplace(
            packet_id,
            control_packet_type::pubcomp,
            sb.buf(),
            std::get<0>(ptr_size),
            std::get<1>(ptr_size));
    }

    template <typename F>
    void async_send_pubcomp(std::uint16_t packet_id, F const& func) {
        send_buffer sb;
        sb.buf()->push_back(static_cast<char>(packet_id >> 8));
        sb.buf()->push_back(static_cast<char>(packet_id & 0xff));
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::pubcomp, 0b0000));
        auto self = this->shared_from_this();
        async_write(
            sb.buf(), std::get<0>(ptr_size), std::get<1>(ptr_size),
            [this, self, packet_id, func](boost::system::error_code const& ec){
                if (func) func(ec);
                if (h_pub_res_sent_) h_pub_res_sent_(packet_id);
            });
    }

    template <typename... Args>
    void async_send_subscribe(
        std::vector<std::tuple<std::reference_wrapper<std::string const>, std::uint8_t>>& params,
        std::uint16_t packet_id,
        std::string const& topic_name,
        std::uint8_t qos, Args... args) {
        params.push_back(std::make_tuple(std::cref(topic_name), qos));
        async_send_subscribe(params, packet_id, args...);
    }

    template <typename F>
    void async_send_subscribe(
        std::vector<std::tuple<std::reference_wrapper<std::string const>, std::uint8_t>>& params,
        std::uint16_t packet_id,
        F const& func) {
        send_buffer sb;
        sb.buf()->push_back(static_cast<char>(packet_id >> 8));
        sb.buf()->push_back(static_cast<char>(packet_id & 0xff));
        for (auto const& e : params) {
            if (!utf8string::is_valid_length(std::get<0>(e))) throw utf8string_length_error();
            if (!utf8string::is_valid_contents(std::get<0>(e))) throw utf8string_contents_error();
            sb.buf()->insert(sb.buf()->size(), encoded_length(std::get<0>(e)));
            sb.buf()->insert(sb.buf()->size(), std::get<0>(e));
            sb.buf()->push_back(std::get<1>(e));
        }
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::subscribe, 0b0010));
        async_write(sb.buf(), std::get<0>(ptr_size), std::get<1>(ptr_size), func);
    }

    template <typename... Args>
    void async_send_suback(
        std::vector<std::uint8_t>& params,
        std::uint16_t packet_id,
        std::uint8_t qos, Args... args) {
        params.push_back(qos);
        async_send_suback(params, packet_id, args...);
    }

    template <typename F>
    void async_send_suback(
        std::vector<std::uint8_t> const& params,
        std::uint16_t packet_id,
        F const& func) {
        send_buffer sb;
        sb.buf()->push_back(static_cast<char>(packet_id >> 8));
        sb.buf()->push_back(static_cast<char>(packet_id & 0xff));
        for (auto const& e : params) {
            sb.buf()->push_back(e);
        }
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::suback, 0b0000));
        async_write(sb.buf(), std::get<0>(ptr_size), std::get<1>(ptr_size), func);
    }

    template <typename... Args>
    void async_send_unsubscribe(
        std::vector<std::reference_wrapper<std::string const>>& params,
        std::uint16_t packet_id,
        std::string const& topic_name,
        Args... args) {
        params.push_back(std::cref(topic_name));
        async_send_unsubscribe(params, packet_id, args...);
    }

    template <typename F>
    void async_send_unsubscribe(
        std::vector<std::reference_wrapper<std::string const>>& params,
        std::uint16_t packet_id,
        F const& func) {
        send_buffer sb;
        sb.buf()->push_back(static_cast<char>(packet_id >> 8));
        sb.buf()->push_back(static_cast<char>(packet_id & 0xff));
        for (auto const& e : params) {
            if (!utf8string::is_valid_length(e)) throw utf8string_length_error();
            if (!utf8string::is_valid_contents(e)) throw utf8string_contents_error();
            sb.buf()->insert(sb.buf()->size(), encoded_length(e));
            sb.buf()->insert(sb.buf()->size(), e);
        }
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::unsubscribe, 0b0010));
        async_write(sb.buf(), std::get<0>(ptr_size), std::get<1>(ptr_size), func);
    }

    template <typename F>
    void async_send_unsuback(
        std::uint16_t packet_id, F const& func) {
        send_buffer sb;
        sb.buf()->push_back(static_cast<char>(packet_id >> 8));
        sb.buf()->push_back(static_cast<char>(packet_id & 0xff));
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::unsuback, 0b0010));
        async_write(sb.buf(), std::get<0>(ptr_size), std::get<1>(ptr_size), func);
    }

    template <typename F>
    void async_send_pingreq(F const& func) {
        send_buffer sb;
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::pingreq, 0b0000));
        async_write(sb.buf(), std::get<0>(ptr_size), std::get<1>(ptr_size), func);
    }

    template <typename F>
    void async_send_pingresp(F const& func) {
        send_buffer sb;
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::pingresp, 0b0000));
        async_write(sb.buf(), std::get<0>(ptr_size), std::get<1>(ptr_size), func);
    }
    template <typename F>
    void async_send_disconnect(F const& func) {
        send_buffer sb;
        auto ptr_size = sb.finalize(make_fixed_header(control_packet_type::disconnect, 0b0000));
        async_write(sb.buf(), std::get<0>(ptr_size), std::get<1>(ptr_size), func);
    }

    // Non blocking (async) write

    class async_packet {
    public:
        async_packet(
            std::shared_ptr<std::string> const& b = nullptr,
            char* p = nullptr,
            std::size_t s = 0,
            async_handler_t h = async_handler_t())
            :
        packet_(b, p, s), handler_(h) {}
        std::shared_ptr<std::string> const& buf() const { return packet_.buf(); }
        char const* ptr() const { return packet_.ptr(); }
        char* ptr() { return packet_.ptr(); }
        std::size_t size() const { return packet_.size(); }
        async_handler_t const& handler() const { return handler_; }
        async_handler_t& handler() { return handler_; }
    private:
        packet packet_;
        async_handler_t handler_;
    };

    template <typename F>
    void async_write(std::shared_ptr<std::string> const& buf, char* ptr, std::size_t size, F const& func) {
        auto self = this->shared_from_this();
        strand_.post(
            [this, self, buf, ptr, size, func]
            () {
                queue_.emplace_back(buf, ptr, size, func);
                if (queue_.size() > 1) return;
                async_write();
            }
        );
    }

    void async_write() {
        auto& elem = queue_.front();
        auto size = elem.size();
        auto const& func = elem.handler();
        auto self = this->shared_from_this();
        as::async_write(
            *socket_,
            as::buffer(elem.ptr(), size),
            strand_.wrap(
                [this, self, size, func]
                (boost::system::error_code const& ec,
                 std::size_t bytes_transferred) {
                    if (func) func(ec);
                    if (ec) { // Error is handled by async_read.
                        queue_.clear();
                        return;
                    }
                    if (size != bytes_transferred) {
                        queue_.clear();
                        throw write_bytes_transferred_error(size, bytes_transferred);
                    }
                    queue_.pop_front();
                    if (!queue_.empty()) {
                        async_write();
                    }
                }
            )
        );
    }

    std::uint16_t acquire_unique_packet_id() {
        LockGuard<Mutex> lck (store_mtx_);
        if (packet_id_.size() == 0xffff - 1) throw packet_id_exhausted_error();
        do {
            if (++packet_id_master_ == 0) ++packet_id_master_;
        } while (!packet_id_.insert(packet_id_master_).second);
        return packet_id_master_;
    }

    bool register_packet_id(std::uint16_t packet_id) {
        if (packet_id == 0) return false;
        LockGuard<Mutex> lck (store_mtx_);
        return packet_id_.insert(packet_id).second;
    }

    static std::uint16_t make_uint16_t(char b1, char b2) {
        return
            ((static_cast<std::uint16_t>(b1) & 0xff)) << 8 |
            (static_cast<std::uint16_t>(b2) & 0xff);
    }

private:
    Strand strand_;
    std::unique_ptr<Socket> socket_;
    std::string host_;
    std::string port_;
    bool connected_;
    std::string client_id_;
    bool clean_session_;
    boost::optional<will> will_;
    char buf_;
    std::uint8_t fixed_header_;
    std::size_t remaining_length_multiplier_;
    std::size_t remaining_length_;
    std::vector<char> payload_;
    close_handler h_close_;
    error_handler h_error_;
    connect_handler h_connect_;
    connack_handler h_connack_;
    publish_handler h_publish_;
    puback_handler h_puback_;
    pubrec_handler h_pubrec_;
    pubrel_handler h_pubrel_;
    pubcomp_handler h_pubcomp_;
    pub_res_sent_handler h_pub_res_sent_;
    subscribe_handler h_subscribe_;
    suback_handler h_suback_;
    unsubscribe_handler h_unsubscribe_;
    unsuback_handler h_unsuback_;
    pingreq_handler h_pingreq_;
    pingresp_handler h_pingresp_;
    disconnect_handler h_disconnect_;
    boost::optional<std::string> user_name_;
    boost::optional<std::string> password_;
    Mutex store_mtx_;
    mi_store store_;
    std::set<std::uint16_t> qos2_publish_handled_;
    std::deque<async_packet> queue_;
    std::uint16_t packet_id_master_;
    std::set<std::uint16_t> packet_id_;
    bool auto_pub_response_;
    bool auto_pub_response_async_;
};

} // namespace mqtt

#endif // MQTT_ENDPOINT_HPP
