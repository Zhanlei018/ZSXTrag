# HXrag

Windows 下的 C++17 卫星频规 RAG 演示。中文 BM25 检索、本地资料/Wiki 同步、Kimi API 问答、引用回传和网页界面均在项目中，不需要向量数据库或 embedding 服务。

## 直接使用（只需填 Kimi Key）

1. 首次使用执行 `copy config.example.json config.json` 创建本地配置；`start.bat` 也会自动完成这一步。
2. 用记事本打开 **`config.json`**，把 key 填到 `kimi_api_key`：

   ```json
   "kimi_api_key": "sk-你的真实key"
   ```

   不要把真实 key 发给他人或提交到公开仓库。也可用环境变量 `MOONSHOT_API_KEY`，环境变量优先。

3. 双击 `start.bat`。首次会自动调用 CMake 和 MinGW 构建，随后打开 <http://127.0.0.1:8790>。
4. 在页面提问。附带的两个 `演示_*.txt` 是**虚构的流程演示资料，不是真实法规**，演示时可换成自己的 UTF-8 资料。

项目已包含 `build/hxrag.exe`，当前电脑上也可在 HXrag 目录执行：

```bat
build\hxrag.exe serve
```

修改源码后双击 `build.bat` 重新构建。默认模型为 `kimi-k3.1-pro`，可在 `config.json` 修改 `kimi_model`。

## 放入自己的资料

支持 `data/` 及其子目录中的 UTF-8 `.txt`、`.md`、`.markdown`、`.json`，单文件最大 2 MB。修改后点击“重新载入知识库”，或执行 `build\hxrag.exe sync`。

JSON 可以是单份对象，也可以是多份对象组成的数组，每份必须有 `content`：

```json
{
  "title": "资料标题",
  "content": "法规正文……",
  "sourceUrl": "https://官方原文链接",
  "country": "国家或地区",
  "topic": "主题"
}
```

检索不需要 Kimi key，可单独验证：

```bat
build\hxrag.exe retrieve "频率使用许可有效期是多久？"
build\hxrag.exe ask "频率使用许可有效期是多久？"
```

## 可选：同步组员的 Wiki

1. 先启动 Wiki 服务，从其启动目录的 `.wiki-data/runtime.json` 找到 `WIKI_RAG_TOKEN`。
2. 把令牌填到 `config.json` 的 `wiki_rag_token`。`wiki_base_url` 默认是 `http://127.0.0.1:8787/api/wiki`，通常不用改。
3. 重启 HXrag，或点击“重新载入知识库”。

HXrag 每次全量读取 `/knowledge` 的已审核知识，避免删除页面后旧索引残留。Wiki 不可用时仍可使用本地资料；手动同步会显示具体错误。也可用 `WIKI_RAG_TOKEN`、`WIKI_BASE_URL` 环境变量。真实法规资料应由组员导入并审核，示例不能代替法规。

## 可选：接入组员的总前端

补丁对应 <https://github.com/swiftqwq/satellite-spectrum-wiki> 当前主分支，将 `/api/rag/chat` 代理到 HXrag，并移除“后端未实现”提示、更新原网关测试。

如果两个项目目录相邻，在总前端仓库根目录执行；否则将补丁路径换成实际位置：

```sh
git apply ../HXrag/integration/frontend.patch
```

然后先运行 HXrag，再按原项目说明运行 `npm run dev`。总前端仍在 `http://127.0.0.1:5173`；`RAG_PORT` 可改代理端口。如果不应用补丁，直接使用 HXrag 自带页面即可，不影响演示。

## HTTP 接口

- `POST /api/rag/chat`：输入 `{"messages":[{"role":"user","content":"问题"}]}`，返回前端约定的 `message/citations/relatedPages`。
- `POST /api/rag/retrieve`：输入 `{"query":"问题"}`，只返回检索结果，不调用模型。
- `POST /api/rag/sync`：重新载入本地资料并同步 Wiki。
- `GET /api/rag/status`、`GET /healthz`：状态和健康检查。

服务只监听 `127.0.0.1:8790`，key 只在服务端使用，不返回浏览器。第三方 JSON 库为 nlohmann/json（MIT），许可证在 `third_party/nlohmann/LICENSE.MIT`。
