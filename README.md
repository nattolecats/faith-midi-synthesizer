# Faith MIDI Synthesizer

## 必須ライブラリについて

| 設定 | 実行ファイルと同じフォルダーに必要な音源DLL |
| --- | --- |
| Type 1 | `rt_player_1.dll` と `mclib.dll` |
| Type 2 | `rt_synth_2.dll` |
| Type 3 | `rt_player_3.dll` と `mclib.dll` |
| Type 4（既定） | `rt_synth_4.dll` |
| Type 5 | `rt_synth_5.dll` |

選択した音源DLLが直下にない場合は、MIDIデバイスを開けず音は鳴りません。
これらの依存DLLはライセンス上の観点から、ソースコード上では配布していません。
インストール済みFaith製品から直下へ手動でコピーしてください。
例えば「Ring Tone Authoring Tool」がインストールされている場合は、これらのDLLは通常、以下のディレクトリに存在します。

```
C:\Program Files (x86)\Faith\Ring Tone Authoring Tool\Tools\
```

## ビルド方法

Visual Studio 2022 C++と `.deps` 内のWindows SDK NuGetパッケージを使用します。
必要な依存関係を揃えたあと、`build.cmd` をダブルクリックしてください。

`build.cmd` は設定アプリ `FaithMidiSettings.exe` と、32/64ビットのドライバー
`FaithMidi32.dll`・`FaithMidi64.dll` を構築します。
設定アプリ、MIDIアプリやデバイス設定画面を閉じてから実行してください。

## 設定画面について

`FaithMidiSettings.exe` を開いて、音量（0〜300%）と音源タイプを設定します。

音源をType 1～5から選択でき、既定はType 4です。
音源タイプの変更は、再生ソフトがMIDIデバイスを開き直したときに反映されます。

音量は0～300%の間で調整可能です。再生中にも反映されます。

## 初回登録・登録解除・MIDIデバイスの設定方法

設定アプリの「デバイスを登録」ボタンから登録します（管理者権限が必要）。
または「登録解除」ボタンで解除することができます。
MIDIデバイスを登録した後は、MIDI設定の出力先に `Faith MIDI Synthesizer` を選択してください。

Windowsの新しいバージョンではMIDIマッパーの設定項目がないことがあります。
その時は CoolSoft MIDIMapper などのサードパーティツールを使用してください。