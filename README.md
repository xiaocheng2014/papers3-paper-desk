# PaperDesk for M5Stack PaperS3

PaperDesk 是运行在 **M5Stack PaperS3** 上的桌面水墨屏固件。设备通过 Wi-Fi 联网，提供天气时钟、儿童识字、电子工牌和 Todo 四种功能，并带有手机可用的配网门户与局域网管理后台。

目标硬件：M5Stack PaperS3（ESP32-S3、960×540 水墨屏、触摸屏、16 MB Flash、8 MB PSRAM）。

## 功能

### 设备主菜单

设备连接 Wi-Fi 后进入竖屏主菜单，提供四个入口：

1. **时钟**
2. **识字**
3. **工牌**
4. **Todo**

时钟和识字页面使用横屏，工牌、Todo 和主菜单使用竖屏。页面不再根据设备姿态自动切换；在任意内容页面向右滑动即可返回主菜单。

### 天气时钟

横屏天气仪表盘包含：

- 当前温度、天气状态、当天最高/最低温度
- 日出、日落和月相
- 风速与风向
- 日期、时间、湿度、Wi-Fi 与电量
- 未来 8 小时天气与温度
- 温度、降水概率、湿度、气压趋势图

时间每分钟刷新一次，天气数据每 30 分钟通过 [Open-Meteo](https://open-meteo.com/) 更新一次，不需要 API Key。普通刷新使用快速水墨刷新，每 30 分钟执行一次高质量刷新以减轻残影。

天气城市可在局域网管理后台中设置。城市名称经 Open-Meteo 地理编码服务转换为经纬度后保存在设备中。

### 识字

内置部编版一年级上册写字表 100 字。每张横屏学习卡显示：

- 米字格大字
- 拼音
- 两组常用词
- 简单例句
- 当前进度

点击屏幕左半区切换到上一字，点击右半区切换到下一字，向右滑动返回主菜单。

当前实现为完整字卡，不包含参考项目中的逐笔笔顺动画。

### 电子工牌

工牌字段包括：

- 公司
- 姓名
- 职务
- 编号
- 联系方式

内置 A–F 共 6 套版式：A、B 支持照片，C–F 为纯文字版式。照片可直接从手机或电脑上传，浏览器会自动完成居中裁剪、缩放为 240×240，并转换为适合水墨屏的 16 级灰度图。

### Todo

- 局域网后台添加、删除、排序和标记待办
- 设备端显示最多 10 条当前待办
- 点击待办左侧方框后进入二次确认页面
- 完成状态持久化，并同步到管理后台

## Wi-Fi 配网与管理后台

设备没有保存 Wi-Fi 时会开启热点：

- 热点名称：`eink-XXXX`，后四位来自设备 MAC 地址
- 热点密码：`159357hui`
- 配网页面：`http://192.168.4.1/`

手机连接热点后，系统通常会自动打开配网页面。选择 2.4 GHz Wi-Fi、输入密码并提交，设备只会在连接成功后保存凭据。

连接家庭网络后，可通过设备获得的 IP 或 `http://paperdesk.local/` 打开管理后台。管理密码默认为 `159357hui`。后台支持：

- 设置天气城市
- 编辑工牌字段和版式
- 自动处理、预览并上传工牌照片
- 添加、删除、排序和完成 Todo
- 清除 Wi-Fi 并重新进入配网

> 当前热点密码和管理密码为固件内常量。部署到不可信网络前，请修改 `firmware/PaperDesk/AppConfig.h` 中的 `kAdminPassword`。

## 设备交互

| 页面 | 操作 | 效果 |
|---|---|---|
| 主菜单 | 点击卡片 | 进入对应功能 |
| 任意内容页面 | 向右滑动 | 返回竖屏主菜单 |
| 识字 | 点击左/右半屏 | 上一字/下一字 |
| Todo | 点击左侧方框 | 打开二次确认 |
| 任意页面 | 长按左上角 5 秒 | 清除 Wi-Fi 并重启配网 |

扬声器与麦克风在初始化时显式关闭，固件不会测试或驱动蜂鸣器。

## 项目结构

```text
firmware/PaperDesk/
├── PaperDesk.ino          # 应用入口、页面状态和触摸交互
├── AppConfig.h            # 密码、时区、刷新周期等配置
├── AppState.*             # 工牌、Todo 与照片持久化
├── ClockView.*            # 水墨屏页面绘制
├── LiteracyContent.*      # 一年级上册 100 字内容
├── WeatherService.*       # 城市解析和 Open-Meteo 天气请求
├── WifiPortal.*           # 配网、Web Server 和管理 API
└── ManagementPage.h       # 内嵌的移动端管理页面
```

小型设置存储在 ESP32 Preferences/NVS 中；工牌灰度照片存储在 FFat 中。

## 开发环境

已验证组合：

- Arduino CLI / Arduino IDE
- M5Stack ESP32 Arduino Core `3.3.9`
- M5Unified `0.2.10`
- M5GFX `0.2.16`
- 开发板 FQBN：`m5stack:esp32:m5stack_papers3`

安装 M5Stack 开发板平台和上述库后，在仓库根目录执行：

```bash
arduino-cli compile \
  --fqbn m5stack:esp32:m5stack_papers3 \
  --build-path .build-paperdesk \
  firmware/PaperDesk
```

## 烧录

先确认串口名称：

```bash
arduino-cli board list
```

然后烧录：

```bash
arduino-cli upload \
  --port /dev/cu.usbmodem14301 \
  --fqbn m5stack:esp32:m5stack_papers3 \
  --input-dir .build-paperdesk \
  firmware/PaperDesk
```

Windows 或 Linux 请将串口替换为实际设备端口。

## 验收建议

1. 首次启动确认出现 `eink-XXXX` 热点并可通过手机完成配网。
2. 联网后确认设备进入竖屏主菜单。
3. 依次进入时钟、识字、工牌和 Todo，并验证右滑返回。
4. 在管理后台设置城市，确认天气数据能够更新。
5. 检查识字 100 字的上一字/下一字切换。
6. 分别检查工牌 A–F 版式以及照片上传。
7. 添加 Todo，在设备端完成二次确认，再检查后台状态。
8. 重启设备，确认 Wi-Fi、工牌、照片和 Todo 数据仍然存在。

## 参考项目

本项目的部分界面设计参考了以下开源或公开项目，但没有直接烧录或合并其固件：

- [squirmen/PaperS3Weather](https://github.com/squirmen/PaperS3Weather)：PaperS3 天气仪表盘布局
- [hongxin/m5paper-shizi](https://github.com/hongxin/m5paper-shizi)：儿童识字卡片交互与版式
- [kvx17/m5paperWeather-Calendar](https://github.com/kvx17/m5paperWeather-Calendar)：早期天气和日历界面调研
