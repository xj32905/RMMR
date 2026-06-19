# 失败样例记录

用于第 6 周报告：记录真实相机下仍然没有解决或需要继续验证的失败情况。

## 样例 1：清晰区域被标记为 BIG

- 素材/截图：`docs/week6_candidates_BIG.png`
- 场景：真实相机画面中红色区域/灯条较清晰，但目标区域在 mask 中面积较大。
- 现象：`debug_mode=candidates` 下可以看到候选区域附近标记 `BIG`，说明轮廓已被找到，但由于面积超过上限被过滤。
- `/armor_result` 输出：过滤过严时 result 模式无法稳定输出有效装甲板四点。
- `debug_mode=red_mask/blue_mask` 观察：`red_mask` 中红色区域清晰可见，说明颜色分割基本有效。
- `debug_mode=candidates` 观察：被过滤原因显示为 `BIG`。
- 推测原因：初始 `max_contour_area=3000.0` 对当前真实相机画面偏小，近距离或高亮区域的 `boundingRect.area()` 容易超过该阈值。
- 下一步尝试：已将 `max_contour_area` 调整为 `50000.0`；后续拿到标准装甲板后继续确认是否需要收回到更合适的范围。

## 样例 2：无标准装甲板时背景红色区域被提取

- 素材/截图：`docs/week6_red_mask.png`
- 场景：尚未拿到标准装甲板，真实相机画面中存在背景红色区域或红色灯条/反光。
- 现象：`red_mask` 中红色区域被明显提取，但这些区域不一定是标准装甲板。
- `/armor_result` 输出：在无标准目标时，可能出现误检或输出退化四点框。
- `debug_mode=red_mask/blue_mask` 观察：HSV 阈值可以提取红色，但无法区分真实装甲板和背景红色物体。
- `debug_mode=candidates` 观察：背景区域可能因为面积、长宽比满足条件而进入候选，或因 `BIG/R<` 被过滤。
- 推测原因：当前算法是简化现场调参版，主链路为 `mask -> contours -> aspect bars -> top2 armor`，只依赖颜色和形状，不具备语义判断能力。
- 下一步尝试：拿到标准装甲板后重新固定参数；必要时启用 `pair_validation` 降低非标准目标误配；第 7 周结合数字分类进一步过滤背景干扰。

## 样例 3：TOP2 简化配对可能产生非标准框

- 素材/截图：`docs/week6_result.png`
- 场景：当前为了便于现场调参，检测逻辑保持为从候选灯条中选择 TOP2 并生成装甲板四点。
- 现象：当画面中没有标准装甲板，或存在多个背景红色长条时，TOP2 可能把非标准区域组合成检测框。
- `/armor_result` 输出：可能仍输出 `detected=true` 和四点坐标，但该框不一定对应真实装甲板。
- `debug_mode=red_mask/blue_mask` 观察：mask 能显示颜色区域，但不能判断装甲板真实性。
- `debug_mode=candidates` 观察：候选排序只反映面积、长度、长宽比等低层特征，可能无法完全排除背景干扰。
- 推测原因：为保持代码简洁和现场可调，默认关闭了 `pair_validation` 和 `strict_lightbar_filter`，因此误检过滤能力有限。
- 下一步尝试：在真实装甲板样例充足后测试 `pair_validation=true`；`strict_lightbar_filter` 作为可选增强项暂不默认开启，避免误杀真实灯条。

---

# W7 错误样例分析（数字识别）

## 样例 4：视频流自动裁剪 ROI 全部分类为 `not_armor`

- **素材/截图**：`docs/w7_result_labeled.png`（red_5000 视频标注结果）
- **场景**：W3 视频检测到装甲板，自动裁剪数字 ROI 后送入 ONNX 分类
- **现象**：视频每帧检测到的装甲板，ONNX 分类均输出 `not_armor`（置信度 0.92~0.98）。由于 `not_armor_threshold=0.7`，`digit_label` 被抑制为空
- **对比**：同一模型对手工裁剪的 `docs/test_input32_0.png` 输出 `one=0.9982`
- **`/armor/result` 输出**：`color="red"`, `digit_label=""`, `confidence=0.0`（被 not_armor 阈值过滤）
- **top3 日志**：`not_armor=0.9692 one=0.0182 base=0.0093`，模型始终偏向 `not_armor`
- **错误来源判断**：**ROI 裁剪阶段为主因**。`cropDigitRoi` 用装甲板中心 + 60% 边长取正方形，不使用透视变换。当装甲板倾斜或数字不在中心时，裁入大量背景/灯条而非数字本身
- **改进方向**：尝试 `warpPerspective` 透视裁正 → 缩小中心正方形比例（60%→40%）→ 中心点下移（数字在装甲板中下部）

## 样例 5：不同曝光度不影响分类 — 排除光照因素

- **素材**：`red_1000_output1.mp4` vs `red_5000_output.mp4` 日志
- **场景**：两个视频曝光差 5 倍，检测轮廓数不同（3 vs 4~8），但分类结果一致
- **现象**：
  - red_1000: `not_armor=0.92~0.98`
  - red_5000: `not_armor=0.95~0.97`
- **`/armor/result` 输出**：两个视频的 `digit_label` 均为空
- **错误来源判断**：**非预处理/光照问题**。模型输入经 `/255.0` 归一化，曝光不影响归一化后的数字特征分布。两个视频分类一致性说明问题不在光照
- **改进方向**：无需从曝光角度优化。进一步确认问题在 ROI 裁剪几何位置

## 样例 6：`mvs_camera_node` 冗余 — 代码维护

- **场景**：`src/mvs_camera_node.cpp` 与 `src/camera_node.cpp` 功能重叠
- **现象**：`mvs_camera_node` 仅支持 BayerRG8 格式，`camera_node` 支持 BGR8/RGB8/Mono8/通用转换，完全覆盖
- **错误来源判断**：**代码维护问题**，非算法问题
- **改进方向**：标记 deprecated 或删除

---

## W7 错误总结

| # | 错误 | 来源 | 严重程度 |
|---|------|------|---------|
| 4 | 自动 ROI 全误判 `not_armor` | ROI 裁剪策略 | **高** |
| 5 | 分类与曝光无关 | 已排除 | - |
| 6 | `mvs_camera_node` 冗余 | 代码维护 | 低 |

**核心改进：优化 `cropDigitRoi` 裁剪策略**（透视变换 + 缩小区域 + 中心偏移）。
