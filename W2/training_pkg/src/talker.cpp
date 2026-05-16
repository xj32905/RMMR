#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

int main(int argc, char **argv) {
    rclcpp::init(argc, argv);
    auto node = rclcpp::Node::make_shared("talker");
    
    // 声明参数 my_param，默认值为 "default_value"
    node->declare_parameter<std::string>("my_param", "default_value");
    std::string my_param = node->get_parameter("my_param").as_string();
    RCLCPP_INFO(node->get_logger(), "Parameter my_param = %s", my_param.c_str());
    
    auto pub = node->create_publisher<std_msgs::msg::String>("chatter", 10);
    rclcpp::WallRate loop_rate(2);
    int count = 0;
    while (rclcpp::ok()) {
        auto msg = std_msgs::msg::String();
        msg.data = "Hello " + std::to_string(count++);
        pub->publish(msg);
        RCLCPP_INFO(node->get_logger(), "Publish: %s", msg.data.c_str());
        rclcpp::spin_some(node);
        loop_rate.sleep();
    }
    rclcpp::shutdown();
    return 0;
}