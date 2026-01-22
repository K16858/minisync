# minisync
> 個人利用を想定した小規模ファイル同期ツール

**minisync**は、PC間でファイルを手動同期するための軽量ツールです。Gitで管理するほどではない雑多なファイルを、明示的に制御しながら安全に共有できます。

## 特徴

- **手動同期**: 自動同期ではなく、PUSHとPULLを明示的に指定
- **マルチspace対応**: 複数のプロジェクトを1つのサーバーで管理
- **安全装置**: 上書き前に自動スナップショット（`.minisync.bak`）
- **トークン認証**: 簡易的なトークンベース認証
- **LAN内自動検出**: UDP broadcastで同一ネットワーク内のspaceを発見
- **バイナリ安全**: あらゆる種類のファイルを転送可能

## インストール

### クイックインストール（推奨）

ワンライナーで自動インストール：

```bash
curl -fsSL https://raw.githubusercontent.com/K16858/minisync/main/install.sh | bash
```

これにより最新リリースのバイナリが`~/.local/bin`にインストールされます。

### ソースからビルド

```bash
git clone https://github.com/K16858/minisync.git
cd minisync/src
make
make install
```

`make install`は`~/.local/bin/msync`と`~/.local/bin/msync-server`にインストールします。

### 手動インストール

1. [Releases](https://github.com/K16858/minisync/releases)から最新版をダウンロード
2. バイナリに実行権限を付与：`chmod +x msync msync-server`
3. PATHの通った場所に配置：`mv msync msync-server ~/.local/bin/`

## 使い方

### 1. 初期化

プロジェクトディレクトリで初期化：

```bash
cd /path/to/your/project
msync init my-project
```

これにより：
- `.msync/`ディレクトリが作成される
- `~/.config/minisync/config.json`にspaceとして登録される
- 一意のID、認証トークン、space名が生成される

### 2. サーバー起動

どこかのマシンでサーバーを起動：

```bash
msync-server
```

サーバーは：
- ポート`61001`でTCP接続を待ち受け
- ポート`61002`でUDP discover要求に応答
- 登録されている全てのspaceを提供

### 3. 他のspaceを検出

LAN内の他のspaceを検出：

```bash
msync discover
```

出力例：
```
#  ID                               NAME            HOSTNAME        IP              PORT
0  e82d4697cf26f4c82092b34a8587743c space-alpha     laptop         192.168.1.100   61001
1  c23234a0d00afb89ddfba000b9c0e0f0 space-beta      desktop        192.168.1.101   61001
```

### 4. 接続先を保存

検出したspaceに接続：

```bash
msync connect 0
# または
msync connect e82d4697cf26f4c82092b34a8587743c
```

### 5. ファイル同期

**PUSH（ローカル → リモート）:**
```bash
msync push myfile.txt
```

**PULL（リモート → ローカル）:**
```bash
msync pull myfile.txt
```

接続先を明示的に指定する場合：
```bash
msync push myfile.txt --host 192.168.1.100 --port 61001
```

## アーキテクチャ

### ディレクトリ構造

```
~/.local/bin/
  ├── msync              # クライアント実行ファイル
  └── msync-server       # サーバー実行ファイル

~/.config/minisync/
  └── config.json        # グローバル設定（全spaceの一覧）

/path/to/project/.msync/
  ├── config.json        # space固有設定（ID, token, name）
  └── targets.json       # 接続先リスト
```

### マルチspace対応

1つのサーバープロセスで複数のspaceを扱います：

- クライアントは接続時に自分のspace IDを送信
- サーバーはspace IDで該当spaceを検索
- 該当spaceのディレクトリに切り替えて処理
- 各spaceのトークンで認証

これにより：
- 全space共通でポート61001を使用（ファイアウォール設定が簡単）
- 1つのサーバーで複数プロジェクトを管理
- 各spaceは完全に独立

## プロトコル

minisyncは独自のバイナリプロトコルを使用：

1. **HELLO**: クライアントがspace IDを送信
2. **HELLO_ACK**: サーバーが応答
3. **TOKEN**: クライアントが認証トークンを送信
4. **PUSH_FILE / PULL_FILE**: ファイル操作要求
5. **META**: ファイルサイズなどのメタデータ
6. **DATA**: 実際のファイルデータ（4KBチャンク）
7. **DONE**: 操作完了
8. **ERROR**: エラー通知

## 設定ファイル

### グローバル設定 (`~/.config/minisync/config.json`)

```json
{
  "spaces": [
    {
      "id": "e82d4697cf26f4c82092b34a8587743c",
      "name": "my-project",
      "path": "/home/user/projects/my-project"
    }
  ]
}
```

### Space設定 (`.msync/config.json`)

```json
{
  "id": "e82d4697cf26f4c82092b34a8587743c",
  "name": "my-project",
  "hostname": "laptop",
  "token": "a59f2921c9b101499d001ac005dad7bc",
  "port": 61001
}
```

## 現在の制限事項

- **ディレクトリ同期**: 未対応（単一ファイルのみ）
- **差分転送**: 未対応（全体転送のみ）
- **ハッシュ検証**: 未対応（サイズチェックのみ）
- **中断/再開**: 未対応
- **暗号化**: 未対応（LAN内での使用を想定）

## 今後の予定

- [ ] SHA-256によるファイル整合性検証
- [ ] ブロック単位の差分転送
- [ ] ディレクトリ全体の同期
- [ ] `.minisyncignore`によるファイル除外
- [ ] 中断/再開機能
- [ ] 進捗表示の改善

## ライセンス

MIT License

## 貢献

Issue、Pull Requestを歓迎します。

## 注意事項

- このツールはLAN内での使用を想定しています
- インターネット経由での使用は推奨しません（暗号化未対応）
- 重要なファイルは必ずバックアップを取ってください
- 本番環境での使用前に十分なテストを行ってください
