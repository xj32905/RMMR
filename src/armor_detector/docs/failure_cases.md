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
