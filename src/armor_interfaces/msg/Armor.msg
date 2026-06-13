# 单个装甲板信息
std_msgs/Header header
string color          # "red" 或 "blue"
string digit_label    # 数字/类型标签，如 "one", "two", "sentry"
float32 confidence    # 数字识别置信度
geometry_msgs/Point32[4] points  # 四点角点：左上、右上、右下、左下
