# Dashboard API 与功能设计

DMU/ICPC 集训队信息面板的功能设计。采用契约优先：`api/` 下的 OpenAPI 文档是
唯一事实源，前端与各语言实现都以它对齐。

本文按**系统功能/子模块**逐个描述：每个模块给出职责、实体（用 TypeScript 表示
数据形状）、接口与权限。TypeScript 仅作说明，唯一事实源仍是 OpenAPI。

## 系统定位与关键决策

- 数据由独立的**采集器**从外部评测平台抓取（早期只采题目元数据，榜单/rating 后续再扩）；
  核心 API 只读本地数据库，不访问外网。
- 信息类内容由 LLM agent 从聊天平台（QQ、Discord 等）采集，经 API 转发到本平台。
- 校内小规模部署；所有接口都需要登录。
- 认证为账号密码 + token；机器人使用长期 API key。
- 实时公告与通知使用 **SSE**。
- 平台同时充当校队的信息公告板；采集机器人按普通账号管理，平台汇聚其提交的内容。
- 权限不做成固定角色：系统中会有大量人类以外的**主体**（采集器、各种
  机器人），因此权限采用可配置的能力（scopes）模型，角色只作为可选标签。

## API 通用约定

- 基础路径 `/api/v1`；破坏性变更走 `/api/v2`，不原地修改。
- JSON，字段 `snake_case`；时间为 UTC ISO-8601；id 为字符串。
- 错误使用 RFC 7807 `application/problem+json`：
  `{ "type", "title", "status", "detail" }`。
- 认证用 `Authorization: Bearer <token>`；token 不透明、随机、服务端存储（可撤销）
  并带过期时间；口令用 Argon2id 哈希。
- 列表接口接受 `page`/`size`、`sort` 与过滤条件，返回 `items` 与 `total`。
- 导入类写接口通过客户端提供的幂等键保证幂等。

通用类型：

```ts
type Id = string;
type Timestamp = string;                 // ISO-8601 UTC

interface ListResponse<T> {
  items: T[];
  total: number;
}
```

## 身份、标签与权限（跨模块）

不采用固定角色，改用四个简单的概念：

- **主体 (principal)**：一切能访问系统的身份——人类账号，或非人类账号（采集器、
  机器人）。
- **标签 (label)**：自由文本，仅用于组织、展示与批量套用模板（如 `member`、
  `manager`、`2023`、`bot`）。**标签本身不决定权限**。
- **权限 (permission / scope)**：形如 `资源:动作` 的能力字符串，是授权的真正单位。
- **凭证 (token / key)**：每次签发绑定一组权限，可带约束（过期、资源范围、速率）。
  人类用登录 token，机器人用长期 key。
- **策略 (policy)**：权限由配置产生——管理员可通过管理接口授予/撤销，也可由配置文件
  （未来的配置中心）声明。标签只是套用"模板"时的便利别名。

这样，接入一个新机器人只需给它一个带最小 scopes 的 key，无需发明新角色；人类账号也走
同一套，默认套一个基础模板，需要时单独加权限。

代表性权限词汇（完整集合以 OpenAPI/策略为准）：

| 权限 | 含义 |
|---|---|
| `account:self` / `account:read` / `account:manage` | 自己的资料 / 查看账号 / 创建账号与签发凭证（含机器人） |
| `member:read` / `member:manage` / `member:assign` | 读名册 / 增删改与导入 / 分配标签与权限 |
| `event:read` / `event:manage` | 查看 / 创建与编辑日历活动 |
| `training:read` / `training:self` / `training:manage` | 查看 / 更新自己的进度 / 管理题单 |
| `announcement:read` / `announcement:write` / `announcement:publish` | 查看 / 起草 / 发布与置顶 |
| `post:read` / `post:write` | 查看 / 发表与编辑帖子 |
| `board:manage` | 创建与编辑板块 |
| `notification:self` | 管理自己的通知 |
| `inbox:submit` / `inbox:review` | 提交采集条目 / 审核 |
| `ingest:write` | 采集器写入 |
| `ops:read` / `ops:metrics` | 运维状态与指标 |

可选**模板**（一次性套用一组默认权限；之后仍可逐项覆盖）：

- `user`：`account:self`、`member:read`、`event:read`、`training:read`、
  `training:self`、`announcement:read`、`post:read`、`post:write`、`notification:self`。
- `manager`：在 `user` 基础上加 `member:manage`、`event:manage`、`training:manage`、
  `announcement:write`、`announcement:publish`、`board:manage`、`inbox:review`。
- `platform`：全部权限（含 `account:manage`、`member:assign`、`ops:*`）。
- 非人类模板：`collector` → `ingest:write`；`bot` → `inbox:submit`。

首个"平台"账号在部署时创建（bootstrap）。`enrollment_year` 等仅用于组织与统计，
不授予权限；其余组织信息（如账号类型）一律用标签表示，便于随时增删。

## 功能模块

### 账号与认证 (auth)

- **职责**：登录、登出、会话凭证、当前主体、绑定 OJ handle；以及账号与凭证的管理。
  **机器人就是普通主体**，持有 `api_key` 凭证，没有单独的实体。
- **实体**：
  ```ts
  interface Principal {
    id: Id;
    username: string;            // 人类登录名；机器人为标识名
    display_name: string;
    labels: string[];            // 组织用途，如 "member" / "manager" / "2023" / "bot"
    permissions: string[];       // 主体级 scope 集合（凭证是其子集或等价）
    enrollment_year?: number;    // 入学年份，如 2023
    created_at: Timestamp;
    disabled_at?: Timestamp | null;
  }

  interface Credential {
    id: Id;
    principal_id: Id;
    kind: 'session' | 'api_key'; // 人类登录用 session，机器人用 api_key
    scopes: string[];            // 该凭证实际拥有的权限
    created_at: Timestamp;
    expires_at?: Timestamp | null;
    last_used_at?: Timestamp | null;
  }

  interface OjHandle {
    id: Id;
    principal_id: Id;
    judge: 'codeforces' | 'atcoder' | 'luogu' | 'nowcoder' | 'vjudge';
    handle: string;
    verified: boolean;
  }
  ```
- **接口**：
  ```
  POST   /api/v1/auth/login              -> token, expires_at, principal
  POST   /api/v1/auth/logout
  GET    /api/v1/me
  PUT    /api/v1/me/oj-handles/{judge}

  POST   /api/v1/accounts                                  (account:manage; 创建账号，含机器人)
  GET    /api/v1/accounts/{id}                             (account:read)
  DELETE /api/v1/accounts/{id}                             (account:manage; 停用)
  POST   /api/v1/accounts/{id}/credentials                 (account:manage; 签发 token / API key，指定 scopes)
  DELETE /api/v1/credentials/{id}                          (account:manage; 撤销)
  ```
- **权限**：登录凭账号密码；`/me` 走 `account:self`；账号与凭证管理走 `account:manage`
  / `account:read`。
- **说明**：机器人与人类同为主体，只是持有 `api_key` 凭证、权限受限；因此没有单独的
  "Agent" 实体，靠标签（如 `bot`）+ 凭证 scopes 区分。

### 成员与队伍 (members & teams)

- **职责**：成员名册、按入学年份分届、队伍/分组、批量导入。
- **实体**：
  ```ts
  interface Team {
    id: Id;
    name: string;
    member_ids: Id[];
  }

  interface TeamMember {
    team_id: Id;
    principal_id: Id;
    role?: string;               // 如 "captain"，仅标签
  }
  ```
- **接口**：
  ```
  GET    /api/v1/members
  GET    /api/v1/members/{id}
  POST   /api/v1/members/import                            (member:manage)
  PUT    /api/v1/members/{id}/labels                       (member:assign)
  PUT    /api/v1/members/{id}/permissions                  (member:assign)
  GET    /api/v1/teams
  GET    /api/v1/teams/{id}
  ```
- **权限**：`member:read` 读名册；导入/改名册用 `member:manage`；标签与权限分配用
  `member:assign`。

### 日历与活动 (calendar & events)

- **职责**：以日历承载训练、比赛、会议、截止日等日程，供成员查看与前端展示。
  （用于替代早期的"比赛与榜单"。）
- **实体**：
  ```ts
  type EventKind = 'training' | 'contest' | 'meeting' | 'deadline' | 'other';

  interface Event {
    id: Id;
    title: string;
    description?: string;
    kind: EventKind;
    all_day: boolean;
    starts_at: Timestamp;
    ends_at?: Timestamp | null;
    location?: string | null;    // 地点或链接
    related_training_id?: Id | null;
    created_by: Id;
    created_at: Timestamp;
    updated_at: Timestamp;
  }
  ```
- **接口**：
  ```
  GET    /api/v1/events?from=&to=&kind=
  GET    /api/v1/events/{id}
  POST   /api/v1/events                                    (event:manage)
  PUT    /api/v1/events/{id}                               (event:manage)
  DELETE /api/v1/events/{id}                               (event:manage)
  ```
- **权限**：读用 `event:read`；增删改用 `event:manage`。
- **说明**：训练题的截止日会同步为 `deadline` 活动；后续接入榜单后可把比赛结果挂到
  `contest` 活动上。

### 训练题单 (trainings)

- **职责**：组织题单（命名题目集合 + 截止时间），跟踪每人进度，可标记"新生轨"。
- **实体**：
  ```ts
  interface Problem {
    id: Id;
    judge: string;
    external_id: string;         // 平台题号
    title: string;
    tags: string[];
    difficulty?: number | null;
    url?: string | null;
  }

  interface Training {
    id: Id;
    title: string;
    description?: string;
    is_newbie_track: boolean;
    due_at?: Timestamp | null;
    problem_ids: Id[];
    created_by: Id;
  }

  interface TrainingProblem {
    training_id: Id;
    problem_id: Id;
    order: number;
  }

  type TrainingProgressState = 'todo' | 'doing' | 'done';

  interface TrainingProgress {
    training_id: Id;
    principal_id: Id;
    problem_id: Id;
    state: TrainingProgressState;
    updated_at: Timestamp;
  }
  ```
- **接口**：
  ```
  GET    /api/v1/trainings
  POST   /api/v1/trainings                                 (training:manage)
  GET    /api/v1/trainings/{id}
  PUT    /api/v1/trainings/{id}                            (training:manage)
  GET    /api/v1/trainings/{id}/progress                   (training:read)
  POST   /api/v1/trainings/{id}/progress                   (training:self)
  ```
- **权限**：管理用 `training:manage`；查看进度用 `training:read`；更新进度用
  `training:self`（仅本人）。
- **说明**：`Problem` 元数据由采集器 ingest 或人工录入。

### 公告 (announcements)

- **职责**：策展广播内容，支持定时发布、置顶、过期与来源引用。
- **实体**：
  ```ts
  interface Announcement {
    id: Id;
    title: string;
    body_markdown: string;
    category: string;
    pinned: boolean;
    published: boolean;
    publish_at?: Timestamp | null;
    expires_at?: Timestamp | null;
    source_item_id?: Id | null;  // 指向 inbox_item
    created_by: Id;
    created_at: Timestamp;
    updated_at: Timestamp;
  }
  ```
- **接口**：
  ```
  GET    /api/v1/announcements
  POST   /api/v1/announcements                             (announcement:write)
  GET    /api/v1/announcements/{id}
  PUT    /api/v1/announcements/{id}                        (announcement:write)
  POST   /api/v1/announcements/{id}/publish                (announcement:publish; 支持 publish_at)
  POST   /api/v1/announcements/{id}/pin                    (announcement:publish)
  GET    /api/v1/announcements/stream                      (SSE)
  ```
- **权限**：读用 `announcement:read`；起草用 `announcement:write`；发布/置顶用
  `announcement:publish`。
- **说明**：来源可指向收件箱条目（见 Agent 模块），并会推送到通知与 SSE。

### 帖子与板块 (posts & boards)

- **职责**：帖子是最原始的内容形态，帖子之间可以相互引用；用**板块**对帖子归类。
  主题/楼层、治理等更完整的讨论形态暂不做。
- **实体**：
  ```ts
  interface Board {
    id: Id;
    name: string;
    description?: string;
    order: number;               // 展示排序
  }

  interface Post {
    id: Id;
    board_id: Id;                // 所属板块
    author_id: Id;
    body_markdown: string;
    references: Id[];            // 引用的其它帖子 id
    created_at: Timestamp;
    updated_at?: Timestamp | null;
    deleted: boolean;
  }
  ```
- **接口**：
  ```
  GET    /api/v1/boards
  GET    /api/v1/boards/{id}
  POST   /api/v1/boards                                    (board:manage)
  PUT    /api/v1/boards/{id}                               (board:manage)
  DELETE /api/v1/boards/{id}                               (board:manage)

  GET    /api/v1/boards/{id}/posts
  POST   /api/v1/boards/{id}/posts                         (post:write)
  GET    /api/v1/posts/{id}
  PUT    /api/v1/posts/{id}                                (作者)
  DELETE /api/v1/posts/{id}                                (作者)
  ```
- **权限**：读用 `post:read`；发表用 `post:write`；帖子编辑/删除仅限作者；板块增删改用
  `board:manage`。
- **说明**：正文 markdown；引用关系存于 `references`。主题/楼层、治理、全文搜索与附件
  等后续再加；公告与之分离，是策展广播。

### 通知 (notifications)

- **职责**：按主体、分类型的站内通知与未读数。
- **实体**：
  ```ts
  type NotificationKind = 'mention' | 'reply' | 'announcement' | 'event_starting';

  interface Notification {
    id: Id;
    principal_id: Id;
    kind: NotificationKind;
    title: string;
    link?: string | null;
    read: boolean;
    created_at: Timestamp;
  }
  ```
- **接口**：
  ```
  GET    /api/v1/notifications
  POST   /api/v1/notifications/read
  GET    /api/v1/notifications/stream                      (SSE)
  ```
- **权限**：`notification:self`（仅本人）。
- **说明**：类型包括 @、回复、公告、活动开始；邮件/推送暂不在范围内。

### 内容汇聚 (inbox)

- **职责**：汇聚外部机器人（按普通账号管理，见"账号与认证"）经 API 提交的内容；
  按提交账号凭证的 scopes 决定直接发布还是送审。
- **实体**：
  ```ts
  type InboxStatus = 'pending' | 'published' | 'rejected' | 'duplicate';

  interface InboxItem {
    id: Id;
    principal_id: Id;            // 提交的账号（机器人）
    platform: string;            // qq / discord / telegram / ...
    url: string;
    author?: string | null;
    raw_text: string;
    summary?: string | null;
    category: string;
    confidence?: number | null;
    dedup_key: string;
    status: InboxStatus;
    announcement_id?: Id | null; // 发布后指向公告
    collected_at: Timestamp;
    created_at: Timestamp;
  }
  ```
- **接口**：
  ```
  POST   /api/v1/inbox/items                               (inbox:submit)
  GET    /api/v1/inbox/items                               (inbox:review; 按 status/principal 过滤)
  POST   /api/v1/inbox/items/{id}/approve                  (inbox:review + announcement:publish)
  POST   /api/v1/inbox/items/{id}/reject                   (inbox:review)
  POST   /api/v1/inbox/items/{id}/merge                    (inbox:review; 去重)
  ```
- **权限**：提交用 `inbox:submit`（机器人账号持有）；审核用 `inbox:review`。
- **说明**：
  - 采集、规范化、去重、摘要都由机器人在外部完成；本项目只接收其提交的条目。
  - 信任：持有 `announcement:publish` 的账号提交即直接发布，否则进入审核队列；每条都
    留档，`/inbox/items` 兼作审计轨迹。
  - 来源信息（平台、url、作者、采集时间）随条目记录并展示。

### 采集器 (collector)

- **职责**：从外部评测平台抓取数据并规范化写入核心。早期只采**题目元数据**（供题单
  引用），榜单与 rating 待相应功能回归后再扩。
- **实体**：
  ```ts
  interface ProblemSample {
    judge: string;
    external_id: string;
    title: string;
    tags: string[];
    difficulty?: number | null;
    url?: string | null;
  }
  ```
- **接口**：
  ```
  POST   /api/v1/ingest/problems                           (ingest:write; 幂等)
  ```
- **权限**：仅持有 `ingest:write` 的凭证。
- **说明**：采集器是独立部署（`apps/collector` 或独立仓库），每平台一个适配器，
  处理限流、分页、重试/退避与字段差异；采集失败不影响核心 API。

### 系统与运维 (ops)

- **职责**：健康检查、就绪探针、指标，以及凭证/权限/系统配置的管理。
- **接口**：
  ```
  GET    /api/v1/health
  GET    /api/v1/ready
  GET    /api/v1/metrics
  ```
- **权限**：健康/就绪可匿名或内网；指标用 `ops:metrics`。

## 横切关注点

### 实时通信 (SSE)

- 公告、通知均提供 `text/event-stream` 流；事件带 `data:` JSON，
  以注释作心跳。
- 浏览器 `EventSource` 无法设置 `Authorization` 头，因此 SSE 认证用查询参数中的短时
  token，或用 session cookie——实现前需定案。
- 核心维护进程内事件总线；ingest 与本地变更发布事件，SSE 连接订阅。这要求服务端具备
  **流式响应**能力（缓冲式 `Response` 不够）。

### 多语言部署

契约是 OpenAPI，任何实现都能提供同一套 API。可由一个入口把部分路径转发给另一实现
（网关方案），也可用标准反向代理按路径分流；两者都不需要同进程内的多语言互操作。

### 分期建议

1. **MVP**：认证与可配置权限、成员（批量导入）与队伍、日历与活动、训练题单、公告、
   帖子与板块、通知、SSE 公告/通知。
2. **其次**：内容汇聚与审核（机器人账号 + 可信自动发布 + 收件箱审计）、
   通知流、OJ 题目元数据采集。
3. **后续**：比赛与榜单、rating 与统计、内部 ELO、资源、全文搜索与附件、
   聊天平台 agent 适配器、网关转发。