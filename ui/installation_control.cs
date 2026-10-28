using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;

namespace MonakaBridge {
 internal static class BridgeText {
  internal static string Language="en";
  static readonly Dictionary<string,string> Japanese=new Dictionary<string,string>(StringComparer.Ordinal){
   {"Open installation profile...","\u30a4\u30f3\u30b9\u30c8\u30fc\u30eb\u8a2d\u5b9a\u3092\u958b\u304f..."},
   {"Create calibration profile (Save As)...","\u8f03\u6b63\u8a2d\u5b9a\u3092\u65b0\u898f\u4fdd\u5b58..."},
   {"Sources and devices","\u5165\u529b\u30bd\u30fc\u30b9\u3068\u30c7\u30d0\u30a4\u30b9"},
   {"Refresh / load configuration","\u4fdd\u5b58\u6e08\u307f\u8a2d\u5b9a\u3092\u8aad\u307f\u76f4\u3059"},
   {"Use selected observation","\u9078\u629e\u3057\u305f\u89b3\u6e2c\u60c5\u5831\u3092\u4f7f\u7528"},
   {"Persistent tracker mapping and profile","\u4fdd\u5b58\u3055\u308c\u308b\u30c8\u30e9\u30c3\u30ab\u30fc\u5bfe\u5fdc\u3068\u5ea7\u6a19\u30d7\u30ed\u30d5\u30a1\u30a4\u30eb"},
   {"New mapping / clear selection","\u65b0\u3057\u3044\u5bfe\u5fdc / \u9078\u629e\u3092\u89e3\u9664"},
   {"Source","\u30bd\u30fc\u30b9"},
   {"Device","\u30c7\u30d0\u30a4\u30b9"},
   {"Logical tracker","\u8ad6\u7406\u30c8\u30e9\u30c3\u30ab\u30fc"},
   {"Profile","\u30d7\u30ed\u30d5\u30a1\u30a4\u30eb"},
   {"Input map","\u5165\u529b\u7a7a\u9593"},
   {"Input map revision","\u5165\u529b\u7a7a\u9593\u306e\u30ea\u30d3\u30b8\u30e7\u30f3"},
   {"World map","\u30ef\u30fc\u30eb\u30c9\u7a7a\u9593"},
   {"Existing space approval (new approval requires separate native review)","\u65e2\u5b58\u306e\u7a7a\u9593\u627f\u8a8d\uff08\u65b0\u898f\u627f\u8a8d\u306b\u306f\u5225\u306e\u30cd\u30a4\u30c6\u30a3\u30d6\u5be9\u67fb\u304c\u5fc5\u8981\uff09"},
   {"Clear existing space approval","\u65e2\u5b58\u306e\u7a7a\u9593\u627f\u8a8d\u3092\u89e3\u9664"},
   {"Save mapping","\u5bfe\u5fdc\u3092\u4fdd\u5b58"},
   {"Show profile approval","\u30d7\u30ed\u30d5\u30a1\u30a4\u30eb\u306e\u627f\u8a8d\u3092\u8868\u793a"},
   {"Import legacy route / alignment","\u65e7\u8a2d\u5b9a\u306e\u51fa\u529b\u7d4c\u8def / \u4f4d\u7f6e\u5408\u308f\u305b\u3092\u53d6\u308a\u8fbc\u3080"},
   {"Shared alignment for the selected source map (metres)","\u9078\u629e\u3057\u305f\u5165\u529b\u7a7a\u9593\u306e\u5171\u901a\u4f4d\u7f6e\u5408\u308f\u305b\uff08\u30e1\u30fc\u30c8\u30eb\uff09"},
   {"Apply shared translation","\u5171\u901a\u306e\u5e73\u884c\u79fb\u52d5\u3092\u9069\u7528"},
   {"Apply output policy","\u51fa\u529b\u65b9\u91dd\u3092\u4fdd\u5b58"},
   {"Start Bridge","Bridge \u3092\u958b\u59cb"},
   {"Stop Bridge","Bridge \u3092\u505c\u6b62"},
   {"Restart Bridge","Bridge \u3092\u518d\u8d77\u52d5"},
   {"Cancel pending save","\u9032\u884c\u4e2d\u306e\u4fdd\u5b58\u3092\u53d6\u308a\u6d88\u3059"},
   {"Start","\u958b\u59cb"},
   {"Stop","\u505c\u6b62"},
   {"Restart","\u518d\u8d77\u52d5"},
   {"Language: English","\u8868\u793a\u8a00\u8a9e: \u82f1\u8a9e"},
   {"Language: Japanese","\u8868\u793a\u8a00\u8a9e: \u65e5\u672c\u8a9e"},
   {"Language is selected in the native tray while this window is closed.","\u8868\u793a\u8a00\u8a9e\u306f\u3001\u3053\u306e\u753b\u9762\u3092\u9589\u3058\u3066\u304b\u3089\u30cd\u30a4\u30c6\u30a3\u30d6\u30c8\u30ec\u30a4\u3067\u9078\u629e\u3067\u304d\u307e\u3059\u3002"},
   {"World calibration and separate output approval","\u30ef\u30fc\u30eb\u30c9\u8f03\u6b63\u3068\u72ec\u7acb\u3057\u305f\u51fa\u529b\u627f\u8a8d"},
   {"Open is resource-free. Discover explicitly starts OpenVR Background and the selected loopback mirror. Choose measurement mode before Open. Static captures paired fresh points; it does not verify physical stillness or accuracy. Saving W requires separate output reapproval. Output approval requires an adopted external criteria policy and matching physical evidence.","\u30bb\u30c3\u30b7\u30e7\u30f3\u3092\u958b\u304f\u3060\u3051\u3067\u306f\u6a5f\u6750\u3092\u4f7f\u7528\u3057\u307e\u305b\u3093\u3002\u300c\u5bfe\u8c61\u3092\u691c\u51fa\u300d\u3067 OpenVR Background \u3068\u6307\u5b9a\u30df\u30e9\u30fc\u3092\u958b\u59cb\u3057\u307e\u3059\u3002\u958b\u59cb\u524d\u306b\u6e2c\u5b9a\u65b9\u5f0f\u3092\u9078\u3093\u3067\u304f\u3060\u3055\u3044\u3002\u9759\u6b62\u70b9\u6e2c\u5b9a\u306f\u65b0\u9bae\u306a\u70b9\u306e\u7d44\u3092\u53d6\u5f97\u3057\u307e\u3059\u304c\u3001\u7269\u7406\u7684\u306a\u9759\u6b62\u3084\u7cbe\u5ea6\u306f\u4fdd\u8a3c\u3057\u307e\u305b\u3093\u3002\u30ef\u30fc\u30eb\u30c9\u4fdd\u5b58\u5f8c\u306e\u51fa\u529b\u306b\u306f\u5225\u9014\u518d\u627f\u8a8d\u304c\u5fc5\u8981\u3067\u3059\u3002\u627f\u8a8d\u306b\u306f\u63a1\u7528\u6e08\u307f\u57fa\u6e96\u3068\u5b9f\u6a5f\u306e\u8a3c\u62e0\u304c\u5fc5\u8981\u3067\u3059\u3002"},
   {"_Measurement mode","\u6e2c\u5b9a\u65b9\u5f0f(_M)"},
   {"Static _points (3..16)","\u9759\u6b62\u70b9\u306e\u6570\uff083\uff5e16\uff09(_P)"},
   {"Fresh _pairs per point (1..4096; total <=16384)","\u5404\u70b9\u306e\u65b0\u9bae\u306a\u7d44\u6570\uff081\uff5e4096\u3001\u5408\u8a0816384\u4ee5\u4e0b\uff09(_F)"},
   {"Trajectory","\u8ecc\u8de1"},
   {"Static","\u9759\u6b62\u70b9"},
   {"Open session","\u30bb\u30c3\u30b7\u30e7\u30f3\u3092\u958b\u304f"},
   {"Discover target","\u5bfe\u8c61\u3092\u691c\u51fa"},
   {"Confirm displayed witness","\u8868\u793a\u3057\u305f\u691c\u8a3c\u60c5\u5831\u3092\u78ba\u8a8d"},
   {"Begin trajectory","\u8ecc\u8de1\u6e2c\u5b9a\u3092\u958b\u59cb"},
   {"Begin static plan","\u9759\u6b62\u70b9\u6e2c\u5b9a\u3092\u958b\u59cb"},
   {"_Capture static point","\u9759\u6b62\u70b9\u3092\u53d6\u5f97(_C)"},
   {"Prepare same result","\u540c\u3058\u6e2c\u5b9a\u7d50\u679c\u306e\u4fdd\u5b58\u6848\u3092\u4f5c\u6210"},
   {"Save exact candidate","\u78ba\u8a8d\u3057\u305f\u4fdd\u5b58\u6848\u3092\u4fdd\u5b58"},
   {"Cancel / close","\u53d6\u6d88 / \u30bb\u30c3\u30b7\u30e7\u30f3\u3092\u9589\u3058\u308b"},
   {"New measurement","\u65b0\u3057\u3044\u6e2c\u5b9a"},
   {"Open separate _output approval review","\u72ec\u7acb\u3057\u305f\u51fa\u529b\u627f\u8a8d\u3092\u958b\u304f(_O)"},
   {"Review output criteria and _evidence","\u51fa\u529b\u57fa\u6e96\u3068\u8a3c\u62e0\u3092\u5be9\u67fb(_E)"},
   {"Prepare selected output approval _diff","\u9078\u629e\u3057\u305f\u51fa\u529b\u627f\u8a8d\u306e\u5dee\u5206\u3092\u4f5c\u6210(_D)"},
   {"_Approve Output (separate transaction)","\u51fa\u529b\u3092\u627f\u8a8d\uff08\u72ec\u7acb\u3057\u305f\u4fdd\u5b58\uff09(_A)"},
   {"I reviewed this exact native candidate and its SHA-256","\u3053\u306e\u30cd\u30a4\u30c6\u30a3\u30d6\u4fdd\u5b58\u6848\u3068 SHA-256 \u3092\u78ba\u8a8d\u3057\u307e\u3057\u305f"},
   {"Output approval members: no default selection. Missing physical criteria/evidence is a native refusal. A holding measurement never approves sibling mounts or body fit.","\u51fa\u529b\u627f\u8a8d\u306e\u5bfe\u8c61\u306f\u660e\u793a\u7684\u306b\u9078\u3093\u3067\u304f\u3060\u3055\u3044\u3002\u5b9f\u6a5f\u57fa\u6e96\u3084\u8a3c\u62e0\u304c\u306a\u3051\u308c\u3070\u30cd\u30a4\u30c6\u30a3\u30d6\u5074\u304c\u62d2\u5426\u3057\u307e\u3059\u3002\u4fdd\u6301\u59ff\u52e2\u306e\u6e2c\u5b9a\u306f\u5225\u30c7\u30d0\u30a4\u30b9\u306e\u53d6\u4ed8\u3051\u3084\u8eab\u4f53\u9069\u5408\u3092\u627f\u8a8d\u3057\u307e\u305b\u3093\u3002"},
   {"Candidate config preview (bounded). Native commit rechecks exact bytes, identity, witness, lock and original config snapshot.","\u4fdd\u5b58\u6848\u306e\u30d7\u30ec\u30d3\u30e5\u30fc\u3067\u3059\u3002\u4fdd\u5b58\u6642\u306b\u30cd\u30a4\u30c6\u30a3\u30d6\u5074\u304c\u6b63\u78ba\u306a\u30d0\u30a4\u30c8\u5217\u3001\u8b58\u5225\u60c5\u5831\u3001\u691c\u8a3c\u60c5\u5831\u3001\u30ed\u30c3\u30af\u3001\u5143\u306e\u8a2d\u5b9a\u3092\u518d\u78ba\u8a8d\u3057\u307e\u3059\u3002"},
   {"Unmapped discovery / explicit rebind","\u672a\u767b\u9332\u30c7\u30d0\u30a4\u30b9\u306e\u691c\u51fa / \u660e\u793a\u7684\u306a\u518d\u5bfe\u5fdc"},
   {"Canonical decoded claims only; not admitted tracking or verified device support. Known input-frame changes are listed separately.","\u516c\u958b\u5951\u7d04\u304b\u3089\u5fa9\u53f7\u3057\u305f\u7533\u544a\u60c5\u5831\u3067\u3059\u3002\u53d7\u7406\u6e08\u307f\u306e\u8ffd\u8de1\u3084\u5bfe\u5fdc\u6a5f\u5668\u306e\u4fdd\u8a3c\u3067\u306f\u3042\u308a\u307e\u305b\u3093\u3002\u65e2\u77e5\u306e\u5165\u529b\u7a7a\u9593\u306e\u5909\u66f4\u306f\u5225\u306b\u8868\u793a\u3057\u307e\u3059\u3002"},
   {"Verified coordinate profile","\u691c\u8a3c\u6e08\u307f\u5ea7\u6a19\u30d7\u30ed\u30d5\u30a1\u30a4\u30eb"},
   {"Existing explicit source contract","\u65e2\u5b58\u306e\u660e\u793a\u7684\u306a\u5165\u529b\u5951\u7d04"},
   {"New logical tracker ID (Create only)","\u65b0\u898f\u306e\u8ad6\u7406\u30c8\u30e9\u30c3\u30ab\u30fcID\uff08\u65b0\u898f\u4f5c\u6210\u7528\uff09"},
   {"Persisted slot (Replace / revision update)","\u4fdd\u5b58\u6e08\u307f\u306e\u67a0\uff08\u7f6e\u63db / \u30ea\u30d3\u30b8\u30e7\u30f3\u66f4\u65b0\uff09"},
   {"Create new logical tracker","\u8ad6\u7406\u30c8\u30e9\u30c3\u30ab\u30fc\u3092\u65b0\u898f\u4f5c\u6210"},
   {"Replace selected offline slot","\u9078\u629e\u3057\u305f\u505c\u6b62\u4e2d\u306e\u67a0\u3092\u7f6e\u63db"},
   {"Update this known input revision","\u65e2\u77e5\u306e\u5165\u529b\u30ea\u30d3\u30b8\u30e7\u30f3\u3092\u66f4\u65b0"},
   {"Ignore (no configuration write)","\u7121\u8996\uff08\u8a2d\u5b9a\u306f\u4fdd\u5b58\u3057\u306a\u3044\uff09"},
   {"Ignored locally. Configuration bytes and revision unchanged.","\u3053\u306e\u753b\u9762\u3067\u7121\u8996\u3057\u307e\u3057\u305f\u3002\u8a2d\u5b9a\u3068\u30ea\u30d3\u30b8\u30e7\u30f3\u306f\u5909\u66f4\u3057\u3066\u3044\u307e\u305b\u3093\u3002"},
   {"Select an unmapped claim or a known input revision mismatch.","\u672a\u767b\u9332\u306e\u7533\u544a\u3001\u307e\u305f\u306f\u65e2\u77e5\u306e\u5165\u529b\u30ea\u30d3\u30b8\u30e7\u30f3\u4e0d\u4e00\u81f4\u3092\u9078\u629e\u3057\u3066\u304f\u3060\u3055\u3044\u3002"},
   {"Create calibration installation profile (Save As)","\u8f03\u6b63\u7528\u30a4\u30f3\u30b9\u30c8\u30fc\u30eb\u8a2d\u5b9a\u3092\u65b0\u898f\u4fdd\u5b58"},
   {"This creates a separate new profile. It does not change Bridge configuration or start a native worker. Select a persisted mapping, the matching calibration helper, configured mirror port and reference role. Session/clock/Standing witnesses are discovered later.","\u5225\u306e\u65b0\u3057\u3044\u30d7\u30ed\u30d5\u30a1\u30a4\u30eb\u3092\u4f5c\u308a\u307e\u3059\u3002Bridge\u8a2d\u5b9a\u3092\u5909\u66f4\u3057\u305f\u308a\u3001\u30cd\u30a4\u30c6\u30a3\u30d6\u30ef\u30fc\u30ab\u30fc\u3092\u958b\u59cb\u3057\u305f\u308a\u306f\u3057\u307e\u305b\u3093\u3002\u4fdd\u5b58\u6e08\u307f\u306e\u5bfe\u5fdc\u3001\u9069\u5408\u3059\u308b\u8f03\u6b63\u30d8\u30eb\u30d1\u30fc\u3001\u8a2d\u5b9a\u6e08\u307f\u306e\u30df\u30e9\u30fc\u30dd\u30fc\u30c8\u3001\u53c2\u7167\u30ed\u30fc\u30eb\u3092\u9078\u3093\u3067\u304f\u3060\u3055\u3044\u3002\u5b9f\u884c\u30bb\u30c3\u30b7\u30e7\u30f3\u7b49\u306e\u691c\u8a3c\u60c5\u5831\u306f\u5f8c\u3067\u691c\u51fa\u3057\u307e\u3059\u3002"},
   {"Profile name","\u30d7\u30ed\u30d5\u30a1\u30a4\u30eb\u540d"},
   {"Calibration helper","\u8f03\u6b63\u30d8\u30eb\u30d1\u30fc"},
   {"Select calibration helper...","\u8f03\u6b63\u30d8\u30eb\u30d1\u30fc\u3092\u9078\u629e..."},
   {"Select the matching product calibrator","\u5bfe\u5fdc\u3059\u308b\u88fd\u54c1\u306e\u8f03\u6b63\u30d8\u30eb\u30d1\u30fc\u3092\u9078\u629e"},
   {"127.0.0.1 mirror port","127.0.0.1 \u306e\u30df\u30e9\u30fc\u30dd\u30fc\u30c8"},
   {"Reference role","\u53c2\u7167\u30ed\u30fc\u30eb"},
   {"I declare this is the configured mirror of one Bridge, with no relay","\u4e2d\u7d99\u306a\u3057\u306e\u5358\u4e00 Bridge \u306b\u8a2d\u5b9a\u3055\u308c\u305f\u30df\u30e9\u30fc\u3067\u3042\u308b\u3053\u3068\u3092\u5ba3\u8a00\u3057\u307e\u3059"},
   {"Read only (measurement allowed, saving disabled)","\u8aad\u307f\u53d6\u308a\u5c02\u7528\uff08\u6e2c\u5b9a\u53ef\u3001\u4fdd\u5b58\u4e0d\u53ef\uff09"},
   {"Mirror/process association is operator declared; process ownership is unverified. No reference role is guessed. Native helper checks its API/features before open and rechecks the exact target during discover/confirm/commit.","\u30df\u30e9\u30fc\u3068\u30d7\u30ed\u30bb\u30b9\u306e\u5bfe\u5fdc\u306f\u64cd\u4f5c\u62c5\u5f53\u8005\u306e\u5ba3\u8a00\u3067\u3059\u3002\u30d7\u30ed\u30bb\u30b9\u6240\u6709\u6a29\u306f\u672a\u78ba\u8a8d\u3067\u3001\u53c2\u7167\u30ed\u30fc\u30eb\u306f\u63a8\u6e2c\u3057\u307e\u305b\u3093\u3002\u30d8\u30eb\u30d1\u30fc\u306f\u958b\u59cb\u524d\u306bAPI\u3068\u6a5f\u80fd\u3092\u691c\u67fb\u3057\u3001\u691c\u51fa\u30fb\u78ba\u8a8d\u30fb\u4fdd\u5b58\u6642\u306b\u5bfe\u8c61\u3092\u518d\u691c\u8a3c\u3057\u307e\u3059\u3002"},
   {"Save As new v2 profile...","\u65b0\u3057\u3044v2\u8a2d\u5b9a\u3068\u3057\u3066\u4fdd\u5b58..."},
   {"Create a separate new calibration profile","\u5225\u306e\u65b0\u3057\u3044\u8f03\u6b63\u8a2d\u5b9a\u3092\u4f5c\u6210"},
   {"Select Bridge installation profile","Bridge \u306e\u30a4\u30f3\u30b9\u30c8\u30fc\u30eb\u8a2d\u5b9a\u3092\u9078\u629e"},
   {"Select the legacy output route file","\u65e7\u51fa\u529b\u7d4c\u8def\u30d5\u30a1\u30a4\u30eb\u3092\u9078\u629e"},
   {"Select the legacy alignment JSON","\u65e7\u4f4d\u7f6e\u5408\u308f\u305bJSON\u3092\u9078\u629e"},
   {"Discard unsaved form edits and open the selected installation? The owned calibration worker will be closed first.","\u672a\u4fdd\u5b58\u306e\u7de8\u96c6\u3092\u7834\u68c4\u3057\u3066\u9078\u629e\u3057\u305f\u8a2d\u5b9a\u3092\u958b\u304d\u307e\u3059\u304b\uff1f \u6240\u6709\u3059\u308b\u8f03\u6b63\u30ef\u30fc\u30ab\u30fc\u3092\u5148\u306b\u9589\u3058\u307e\u3059\u3002"},
   {"Review explicit discovery mapping","\u660e\u793a\u7684\u306a\u691c\u51fa\u5bfe\u5fdc\u3092\u78ba\u8a8d"},
   {"Technical diagnostics: ","\u6280\u8853\u8a3a\u65ad\uff08\u539f\u6587\uff09: "},
   {"UNCONFIRMED: ","\u672a\u78ba\u8a8d: "},
   {"Installation: ","\u30a4\u30f3\u30b9\u30c8\u30fc\u30eb: "},
   {"Config: ","\u8a2d\u5b9a: "},
   {"Health: ","\u72b6\u614b: "},
   {"Helper: ","\u30d8\u30eb\u30d1\u30fc: "},
   {"Profile approved: ","\u30d7\u30ed\u30d5\u30a1\u30a4\u30eb\u627f\u8a8d: "},
   {"Loaded revision ","\u8aad\u307f\u8fbc\u3093\u3060\u30ea\u30d3\u30b8\u30e7\u30f3: "},
   {"Unknown profile: ","\u4e0d\u660e\u306a\u30d7\u30ed\u30d5\u30a1\u30a4\u30eb: "},
   {"Bridge health unavailable: ","Bridge \u306e\u72b6\u614b\u3092\u53d6\u5f97\u3067\u304d\u307e\u305b\u3093: "},
   {"Bridge health unavailable; previous values retained. ","\u72b6\u614b\u3092\u53d6\u5f97\u3067\u304d\u306a\u3044\u305f\u3081\u3001\u524d\u56de\u306e\u5024\u3092\u4fdd\u6301\u3057\u3066\u3044\u307e\u3059\u3002"},
   {"Select a source/device to inspect live pose.","\u30bd\u30fc\u30b9\u3068\u30c7\u30d0\u30a4\u30b9\u3092\u9078\u629e\u3059\u308b\u3068\u73fe\u5728\u306e\u59ff\u52e2\u3092\u78ba\u8a8d\u3067\u304d\u307e\u3059\u3002"},
   {"Saving; cancellation is checked before native commit.","\u4fdd\u5b58\u4e2d\u3067\u3059\u3002\u30cd\u30a4\u30c6\u30a3\u30d6\u4fdd\u5b58\u306e\u958b\u59cb\u524d\u306b\u53d6\u6d88\u8981\u6c42\u3092\u78ba\u8a8d\u3057\u307e\u3059\u3002"},
   {"Cancellation requested. A native commit already started may finish; wait for its result.","\u53d6\u6d88\u3092\u8981\u6c42\u3057\u307e\u3057\u305f\u3002\u958b\u59cb\u6e08\u307f\u306e\u30cd\u30a4\u30c6\u30a3\u30d6\u4fdd\u5b58\u306f\u5b8c\u4e86\u3059\u308b\u5834\u5408\u304c\u3042\u308a\u307e\u3059\u3002\u7d50\u679c\u3092\u5f85\u3063\u3066\u304f\u3060\u3055\u3044\u3002"},
   {"Owned shutdown unconfirmed; window retained: ","\u6240\u6709\u5bfe\u8c61\u306e\u7d42\u4e86\u306f\u672a\u78ba\u8a8d\u3067\u3059\u3002\u753b\u9762\u3092\u4fdd\u6301\u3057\u307e\u3059: "},
   {"No mapping selected; select a profile explicitly. Save updates the exact source/device or creates a new mapping.","\u5bfe\u5fdc\u304c\u672a\u9078\u629e\u3067\u3059\u3002\u30d7\u30ed\u30d5\u30a1\u30a4\u30eb\u3092\u660e\u793a\u7684\u306b\u9078\u3093\u3067\u304f\u3060\u3055\u3044\u3002\u4fdd\u5b58\u306f\u540c\u3058\u30bd\u30fc\u30b9\u3068\u30c7\u30d0\u30a4\u30b9\u3092\u66f4\u65b0\u3059\u308b\u304b\u3001\u65b0\u898f\u306e\u5bfe\u5fdc\u3092\u4f5c\u6210\u3057\u307e\u3059\u3002"},
   {"SAVED; reload failed: ","\u4fdd\u5b58\u6e08\u307f\u3067\u3059\u304c\u3001\u518d\u8aad\u8fbc\u307f\u306b\u5931\u6557\u3057\u307e\u3057\u305f: "},
   {"SAVED. Runtime application unconfirmed; Core/IK/SteamVR output unverified. Review matching-instance health separately. Rebinding may require an owned runtime restart (unsupported here).","\u4fdd\u5b58\u3057\u307e\u3057\u305f\u3002\u5b9f\u884c\u6642\u306e\u9069\u7528\u3068 Core/IK/SteamVR \u51fa\u529b\u306f\u672a\u78ba\u8a8d\u3067\u3059\u3002\u5bfe\u5fdc\u3059\u308b\u5b9f\u884c\u500b\u4f53\u306e\u72b6\u614b\u3092\u5225\u9014\u78ba\u8a8d\u3057\u3066\u304f\u3060\u3055\u3044\u3002\u518d\u5bfe\u5fdc\u3067\u306f\u6240\u6709\u30e9\u30f3\u30bf\u30a4\u30e0\u306e\u518d\u8d77\u52d5\u304c\u5fc5\u8981\u306a\u5834\u5408\u304c\u3042\u308a\u307e\u3059\u3002"},
   {"Read-only target: runtime controls disabled","\u8aad\u307f\u53d6\u308a\u5c02\u7528\u306e\u305f\u3081\u30e9\u30f3\u30bf\u30a4\u30e0\u64cd\u4f5c\u306f\u7121\u52b9\u3067\u3059"},
   {"Save in progress","\u4fdd\u5b58\u4e2d"},
   {"Reload/review required","\u518d\u8aad\u8fbc\u307f\u3068\u78ba\u8a8d\u304c\u5fc5\u8981\u3067\u3059"},
   {"Owned shutdown / target change in progress","\u6240\u6709\u5bfe\u8c61\u306e\u7d42\u4e86 / \u8a2d\u5b9a\u306e\u5207\u66ff\u4e2d"},
   {"Calibration session owns this window; close it before config edits","\u8f03\u6b63\u30bb\u30c3\u30b7\u30e7\u30f3\u3092\u9589\u3058\u3066\u304b\u3089\u8a2d\u5b9a\u3092\u7de8\u96c6\u3057\u3066\u304f\u3060\u3055\u3044"},
   {"No native worker","\u30cd\u30a4\u30c6\u30a3\u30d6\u30ef\u30fc\u30ab\u30fc\u306f\u505c\u6b62\u4e2d"}
  };
  internal static string T(string value){string translated;return Language=="ja"&&value!=null&&Japanese.TryGetValue(value,out translated)?translated:value;}
  internal static string Diagnostic(string value){return Language=="ja"?T("Technical diagnostics: ")+value:value;}
 }

 // The existing tray remains the only runtime owner. This versioned local
 // channel conveys explicit GUI commands; it is not an input protocol.
 internal sealed class BridgeOwnerChannel : IDisposable {
  readonly object gate=new object();readonly string pipeName,configIdentity;
  readonly Func<string,string> command;readonly Thread worker;
  readonly System.IO.MemoryMappedFiles.MemoryMappedFile identityMap;
  System.IO.Pipes.NamedPipeServerStream active;bool disposed;
  [System.Runtime.InteropServices.DllImport("kernel32.dll",SetLastError=true)]
  static extern bool GetNamedPipeServerProcessId(Microsoft.Win32.SafeHandles.SafePipeHandle pipe,out uint pid);
  static string ExecutableHash(string path){using(var input=File.OpenRead(path))using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(input)).Replace("-","").ToLowerInvariant();}
  internal static string Scope(string config,string ingress){
   // Same configuration cannot acquire a second owner by changing endpoints.
   using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(Path.GetFullPath(config).ToUpperInvariant()))).Replace("-","").ToLowerInvariant();
  }
  internal static void Validate(string pipe,string identity){
   const string prefix="MonakaBridgeOwnerV1_";Guid nonce;if(!pipe.StartsWith(prefix,StringComparison.Ordinal)||!Guid.TryParseExact(pipe.Substring(prefix.Length),"N",out nonce))throw new ArgumentException("Invalid owner channel");
   var parts=identity.Split(':');int pid;long created;
   if(parts.Length!=2||!int.TryParse(parts[0],out pid)||pid<=0||!long.TryParse(parts[1],out created)||created<=0)throw new ArgumentException("Invalid owner identity");
  }
  internal BridgeOwnerChannel(GuiLaunchOptions options,Func<string,string> action){
   pipeName="MonakaBridgeOwnerV1_"+Guid.NewGuid().ToString("N");configIdentity=Scope(options.ConfigFile,"");command=action;
   using(var self=Process.GetCurrentProcess()){
    long created=self.StartTime.ToUniversalTime().ToFileTimeUtc();
    identityMap=System.IO.MemoryMappedFiles.MemoryMappedFile.CreateNew(@"Local\MonakaBridge_Controller_"+configIdentity,48);
    using(var view=identityMap.CreateViewAccessor()){
     view.Write(0,0x314c4350u);view.Write(4,1u);view.Write(8,1L);view.Write(16,(uint)self.Id);view.Write(20,(uint)self.Id);view.Write(24,created);view.Write(32,created);view.Write(40,1u);Thread.MemoryBarrier();view.Write(8,2L);
    }
    options.OwnerPipe=pipeName;options.OwnerIdentity=self.Id+":"+created;
   }
   worker=new Thread(Serve){IsBackground=true,Name="Bridge owned GUI requests"};worker.Start();
  }
  void Serve(){
   while(true){
    System.IO.Pipes.NamedPipeServerStream pipe;
    lock(gate){if(disposed)return;pipe=new System.IO.Pipes.NamedPipeServerStream(pipeName,System.IO.Pipes.PipeDirection.InOut,1,System.IO.Pipes.PipeTransmissionMode.Byte,System.IO.Pipes.PipeOptions.Asynchronous);active=pipe;}
    try{pipe.WaitForConnection();using(var timeout=new Timer(delegate{try{pipe.Dispose();}catch{}},null,15000,Timeout.Infinite)){
     byte[] request=new byte[66];int got=0;while(got<request.Length){int n=pipe.Read(request,got,request.Length-got);if(n==0)throw new EndOfStreamException();got+=n;}
     string result;
     try{
      if(request[0]!=1||Encoding.ASCII.GetString(request,2,64)!=configIdentity)throw new IOException("Owner target/version mismatch");
      string operation=request[1]==1?"Start":request[1]==2?"Stop":request[1]==3?"Restart":null;
      if(operation==null)throw new IOException("Unknown owner command");
      result="OK "+command(operation);
     }catch(Exception e){result="UNCONFIRMED "+e.Message;}
     byte[] response=Encoding.UTF8.GetBytes(result);if(response.Length>4096)response=Encoding.UTF8.GetBytes("UNCONFIRMED Owner diagnostic exceeds bound");
     byte[] length=BitConverter.GetBytes(response.Length);pipe.Write(length,0,4);pipe.Write(response,0,response.Length);pipe.Flush();if(pipe.ReadByte()!=1)throw new IOException("Owner response acknowledgement missing");
    }}catch(IOException){}catch(ObjectDisposedException){}
    finally{lock(gate){if(object.ReferenceEquals(active,pipe))active=null;}pipe.Dispose();}
   }
  }
  internal static string Request(GuiLaunchOptions options,string operation){
   Validate(options.OwnerPipe,options.OwnerIdentity);var identity=options.OwnerIdentity.Split(':');
   using(var owner=Process.GetProcessById(int.Parse(identity[0])))
   using(var self=Process.GetCurrentProcess())
   using(var pipe=new System.IO.Pipes.NamedPipeClientStream(".",options.OwnerPipe,System.IO.Pipes.PipeDirection.InOut,System.IO.Pipes.PipeOptions.Asynchronous)){
    string expected=options.OwnerExecutable??self.MainModule.FileName;
    string expectedHash=options.OwnerSha256??ExecutableHash(self.MainModule.FileName);
    if(owner.Handle==IntPtr.Zero||owner.StartTime.ToUniversalTime().ToFileTimeUtc()!=long.Parse(identity[1])||owner.HasExited||
      !string.Equals(owner.MainModule.FileName,expected,StringComparison.OrdinalIgnoreCase)||ExecutableHash(owner.MainModule.FileName)!=expectedHash)throw new IOException("Owning tray process identity changed");
    pipe.Connect(2000);uint server;
    if(!GetNamedPipeServerProcessId(pipe.SafePipeHandle,out server)||server!=(uint)owner.Id)throw new IOException("Owner pipe belongs to another process");
    using(var timeout=new Timer(delegate{try{pipe.Dispose();}catch{}},null,12000,Timeout.Infinite)){
     byte opcode=operation=="Start"?(byte)1:operation=="Stop"?(byte)2:operation=="Restart"?(byte)3:operation=="Probe"?(byte)6:operation=="Status"?(byte)8:(byte)0;
     if(opcode==0)throw new ArgumentException("Unsupported service command");
     byte[] request=new byte[66];request[0]=1;request[1]=opcode;Encoding.ASCII.GetBytes(Scope(options.ConfigFile,"")).CopyTo(request,2);pipe.Write(request,0,request.Length);pipe.Flush();
     var reader=new BinaryReader(pipe,Encoding.UTF8);int length=reader.ReadInt32();if(length<1||length>4096)throw new IOException("Owner response length invalid");
     byte[] response=reader.ReadBytes(length);if(response.Length!=length)throw new IOException("Owner response incomplete; outcome unknown");
     pipe.WriteByte(1);pipe.Flush();
     string result=Encoding.UTF8.GetString(response);if(!result.StartsWith("OK ",StringComparison.Ordinal))throw new IOException(result);return result.Substring(3);
    }
   }
  }
  public void Dispose(){lock(gate){if(disposed)return;disposed=true;if(active!=null)active.Dispose();}if(!worker.Join(16000))throw new IOException("Owner channel shutdown unconfirmed");identityMap.Dispose();}
 }
 // Local GUI control-plane configuration, never a tracking/wire contract.
 internal sealed class GuiLaunchOptions {
  internal readonly string Root,ConfigFile,HealthFile,ConfigHelper,HelperSha256,InstallationName;
  internal readonly bool ReadOnly,ControlWindowOnly;
  internal readonly CalibrationInstallation Calibration;
  internal string OwnerPipe,OwnerIdentity,OwnerExecutable,OwnerSha256;
  internal string Language="en";
  GuiLaunchOptions(string root,string config,string health,string helper,string hash,string name,bool readOnly,bool window,CalibrationInstallation calibration=null){
   Root=root;ConfigFile=config;HealthFile=health;ConfigHelper=helper;HelperSha256=hash;InstallationName=name;ReadOnly=readOnly;ControlWindowOnly=window;Calibration=calibration;
  }
  internal static GuiLaunchOptions FromRoot(string root){return Parse(new[]{root},root);}
  internal static string Absolute(string value,string option){
   if(string.IsNullOrWhiteSpace(value)||value.IndexOf('"')>=0||value.IndexOf('\0')>=0||!Path.IsPathRooted(value)||
     !(value.StartsWith(@"\\",StringComparison.Ordinal)||(value.Length>2&&char.IsLetter(value[0])&&value[1]==':'&&(value[2]=='\\'||value[2]=='/'))))
    throw new ArgumentException(option+" requires an absolute path.");
   return Path.GetFullPath(value);
  }
  static string ProfilePath(Dictionary<string,object> profile,string key,string directory){
   object raw;if(!profile.TryGetValue(key,out raw)||!(raw is string)||string.IsNullOrWhiteSpace((string)raw))throw new ArgumentException("Installation requires "+key);
   string value=(string)raw;
   return Absolute(Path.IsPathRooted(value)?value:Path.Combine(directory,value),key);
  }
  internal static GuiLaunchOptions Parse(string[] args,string defaultRoot){
   string root=null,config=null,health=null,helper=null,hash=null,installation=null,name=null,profileHash=null,ownerPipe=null,ownerIdentity=null,ownerExecutable=null,ownerSha256=null,language="en";
   bool readOnly=false,allowWrite=false,window=false,profileReadOnly=true;
   var seen=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
   for(int i=0;args!=null&&i<args.Length;i++){
    string arg=args[i];if(string.IsNullOrWhiteSpace(arg))throw new ArgumentException("Empty GUI argument");
    if(!arg.StartsWith("--",StringComparison.Ordinal)){if(root!=null)throw new ArgumentException("Only one root is allowed");root=Path.GetFullPath(arg);continue;}
    if(!seen.Add(arg))throw new ArgumentException("Duplicate option: "+arg);
    switch(arg.ToLowerInvariant()){
     case "--read-only":readOnly=true;break;
     case "--allow-write":allowWrite=true;break;
     case "--control-window-only":window=true;break;
     case "--language":case "--owner-executable":case "--owner-sha256":case "--owner-pipe":case "--owner-identity":case "--installation":case "--config":case "--health":case "--config-helper":case "--helper-sha256":case "--installation-name":case "--profile-sha256":
      if(++i>=args.Length||string.IsNullOrWhiteSpace(args[i])||args[i].StartsWith("--",StringComparison.Ordinal))throw new ArgumentException("Missing value for "+arg);
      switch(arg.ToLowerInvariant()){
       case "--language":language=args[i];if(language!="en"&&language!="ja")throw new ArgumentException("Language must be en or ja");break;
       case "--owner-pipe":ownerPipe=args[i];break;
       case "--owner-identity":ownerIdentity=args[i];break;
       case "--owner-executable":ownerExecutable=Absolute(args[i],arg);break;
       case "--owner-sha256":ownerSha256=args[i].ToLowerInvariant();break;
       case "--installation":installation=Absolute(args[i],arg);break;
       case "--config":config=Absolute(args[i],arg);break;
       case "--health":health=Absolute(args[i],arg);break;
       case "--config-helper":helper=Absolute(args[i],arg);break;
       case "--helper-sha256":hash=args[i];break;
       case "--installation-name":name=args[i];break;
       case "--profile-sha256":profileHash=args[i];break;
      }break;
     default:throw new ArgumentException("Unknown GUI option: "+arg);
    }
   }
   if((ownerPipe==null)!=(ownerIdentity==null))throw new ArgumentException("Incomplete owner endpoint");
   if((ownerExecutable==null)!=(ownerSha256==null)||ownerExecutable!=null&&ownerPipe==null)throw new ArgumentException("Incomplete native owner pin");
   if(ownerSha256!=null){if(ownerSha256.Length!=64)throw new ArgumentException("Invalid native owner SHA256");foreach(char c in ownerSha256)if(!Uri.IsHexDigit(c))throw new ArgumentException("Invalid native owner SHA256");}
   if(ownerPipe!=null)BridgeOwnerChannel.Validate(ownerPipe,ownerIdentity);
   if(readOnly&&allowWrite)throw new ArgumentException("Read-only and allow-write conflict");
   bool explicitPaths=config!=null||health!=null||helper!=null||hash!=null;
   if(profileHash!=null&&(installation==null||profileHash.Length!=64))throw new ArgumentException("Profile snapshot hash requires an installation and64 hexadecimal characters");
   if(profileHash!=null)foreach(char c in profileHash)if(!Uri.IsHexDigit(c))throw new ArgumentException("Invalid profile snapshot SHA256");
   if(installation!=null){
    if(explicitPaths||name!=null)throw new ArgumentException("Installation profile cannot be combined with path/name overrides");
    var info=new FileInfo(installation);if(!info.Exists||info.Length>65536)throw new ArgumentException("Installation profile missing or too large");
    byte[] profileBytes=CalibrationInstallation.ReadBytes(installation);
    string legacyText;using(var reader=new StreamReader(new MemoryStream(profileBytes),Encoding.UTF8,true))legacyText=reader.ReadToEnd();
    var p=new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(legacyText);
    object schema;
    if(p.TryGetValue("schema",out schema)&&object.Equals(schema,CalibrationInstallation.Schema)){
     var selected=CalibrationInstallation.Parse(installation,profileBytes);
     if(profileHash!=null&&!string.Equals(profileHash,selected.ProfileSha256,StringComparison.OrdinalIgnoreCase))throw new ArgumentException("Selected profile changed since launch; reopen explicitly");
     bool effectiveReadonly=readOnly||selected.ReadOnly;
     return new GuiLaunchOptions(Path.GetFullPath(root??Path.GetDirectoryName(installation)),selected.ConfigFile,selected.HealthFile,selected.ConfigHelper,selected.ConfigHelperSha256,selected.Name,effectiveReadonly,window,selected){OwnerPipe=ownerPipe,OwnerIdentity=ownerIdentity,OwnerExecutable=ownerExecutable,OwnerSha256=ownerSha256,Language=language};
    }
    if(profileHash!=null)throw new ArgumentException("Profile snapshot requires the selected v2 schema; no fallback");
    var keys=new HashSet<string>(new[]{"schema","name","config","health","config_helper","helper_sha256","read_only"});
    foreach(string key in p.Keys)if(!keys.Contains(key))throw new ArgumentException("Unknown installation field: "+key);
    if(!p.ContainsKey("schema")||!object.Equals(p["schema"],"monaka.bridge.installation.v1"))throw new ArgumentException("Unsupported installation profile schema");
    if(!p.ContainsKey("name")||!(p["name"] is string)||string.IsNullOrWhiteSpace((string)p["name"]))throw new ArgumentException("Installation name required");
    name=(string)p["name"];string dir=Path.GetDirectoryName(installation);
    config=ProfilePath(p,"config",dir);health=ProfilePath(p,"health",dir);helper=ProfilePath(p,"config_helper",dir);
    if(p.ContainsKey("helper_sha256")){if(!(p["helper_sha256"] is string))throw new ArgumentException("Helper hash must be a string");hash=(string)p["helper_sha256"];}
    if(p.ContainsKey("read_only")){if(!(p["read_only"] is bool))throw new ArgumentException("read_only must be boolean");profileReadOnly=(bool)p["read_only"];}
    root=root??dir;
   }
   root=Path.GetFullPath(root??defaultRoot??AppDomain.CurrentDomain.BaseDirectory);
   config=config??Path.Combine(root,"config","bridge.json");health=health??config+".status.json";helper=helper??Path.Combine(root,"build","Release","monaka_bridge_config.exe");
   var distinct=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
   foreach(string value in new[]{config,health,helper})if(!distinct.Add(Path.GetFullPath(value)))throw new ArgumentException("Config, health and helper paths must be distinct");
   if(hash!=null){if(hash.Length!=64)throw new ArgumentException("Helper SHA256 must have64 hexadecimal characters");foreach(char c in hash)if(!Uri.IsHexDigit(c))throw new ArgumentException("Invalid helper SHA256");hash=hash.ToLowerInvariant();}
   if(name!=null&&(name.Length>128||name.IndexOfAny(new[]{'\r','\n','\0'})>=0))throw new ArgumentException("Invalid installation name");
   bool ro=readOnly||(!allowWrite&&(installation!=null?profileReadOnly:explicitPaths));
   return new GuiLaunchOptions(root,config,health,helper,hash,name??"Legacy root target",ro,window){OwnerPipe=ownerPipe,OwnerIdentity=ownerIdentity,OwnerExecutable=ownerExecutable,OwnerSha256=ownerSha256,Language=language};
  }
  internal static string Quote(string value){
   if(value==null||value.IndexOf('\0')>=0)throw new ArgumentException("Invalid command argument");
   var b=new StringBuilder("\"");int slashes=0;
   foreach(char c in value){if(c=='\\'){slashes++;continue;}if(c=='"'){b.Append('\\',slashes*2+1);b.Append(c);}else{b.Append('\\',slashes);b.Append(c);}slashes=0;}
   b.Append('\\',slashes*2);return b.Append('"').ToString();
  }
  internal string OwnerArguments(){return OwnerPipe==null?"":" --owner-pipe "+Quote(OwnerPipe)+" --owner-identity "+Quote(OwnerIdentity)+(OwnerExecutable==null?"":" --owner-executable "+Quote(OwnerExecutable)+" --owner-sha256 "+OwnerSha256);}
  internal GuiLaunchOptions RetainMatchingOwner(GuiLaunchOptions next){
   if(OwnerPipe==null)return next;
   bool same=string.Equals(ConfigFile,next.ConfigFile,StringComparison.OrdinalIgnoreCase)&&string.Equals(HealthFile,next.HealthFile,StringComparison.OrdinalIgnoreCase)&&string.Equals(ConfigHelper,next.ConfigHelper,StringComparison.OrdinalIgnoreCase)&&string.Equals(HelperSha256,next.HelperSha256,StringComparison.OrdinalIgnoreCase);
   if(!same){if(OwnerExecutable!=null)throw new InvalidOperationException("Selected profile targets another installation; open through its own Agent");return next;}
   return new GuiLaunchOptions(next.Root,next.ConfigFile,next.HealthFile,next.ConfigHelper,next.HelperSha256,next.InstallationName,ReadOnly||next.ReadOnly,next.ControlWindowOnly,next.Calibration){OwnerPipe=OwnerPipe,OwnerIdentity=OwnerIdentity,OwnerExecutable=OwnerExecutable,OwnerSha256=OwnerSha256,Language=Language};
  }
  internal string WindowArguments(){return WindowTargetArguments()+OwnerArguments()+" --language "+Language;}
  string WindowTargetArguments(){if(Calibration!=null)return Quote(Root)+" --control-window-only --installation "+Quote(Calibration.ProfilePath)+" --profile-sha256 "+Calibration.ProfileSha256+(ReadOnly?" --read-only":" --allow-write");return Quote(Root)+" --control-window-only --installation-name "+Quote(InstallationName)+" --config "+Quote(ConfigFile)+" --health "+Quote(HealthFile)+" --config-helper "+Quote(ConfigHelper)+(HelperSha256==null?"":" --helper-sha256 "+HelperSha256)+(ReadOnly?" --read-only":" --allow-write");}
  internal string EditBlockReason(){
   if(ReadOnly)return "Read-only installation: editing is disabled";
   if(!File.Exists(ConfigFile))return "Configuration unavailable: "+ConfigFile;
   try{
    if((File.GetAttributes(ConfigFile)&FileAttributes.ReadOnly)!=0)return "Configuration is read-only: "+ConfigFile;
    using(var file=new FileStream(ConfigFile,FileMode.Open,FileAccess.ReadWrite,FileShare.ReadWrite|FileShare.Delete)){}
   }catch(Exception e){return "Configuration permission/access unavailable: "+e.Message;}
   if(!File.Exists(ConfigHelper))return "Config helper unavailable: "+ConfigHelper;
   try{VerifyHelper();}catch(Exception e){return e.Message;}
   return null;
  }
  internal void RequireEdit(){string reason=EditBlockReason();if(reason!=null)throw new InvalidOperationException(reason);}
  internal void VerifyHelper(){
   if(!File.Exists(ConfigHelper))throw new InvalidOperationException("Config helper unavailable: "+ConfigHelper);
   if(HelperSha256!=null){using(var input=File.OpenRead(ConfigHelper))using(var sha=SHA256.Create()){
    string actual=BitConverter.ToString(sha.ComputeHash(input)).Replace("-","").ToLowerInvariant();
    if(actual!=HelperSha256)throw new InvalidOperationException("Config helper SHA256 mismatch");
   }}
  }
  internal string ValidateEditing(){
   string reason=EditBlockReason();if(reason!=null)return reason;
   try{new ConfigHelperClient(this).CheckCompatibility();return null;}catch(Exception e){return e.Message;}
  }
  internal string ServiceBlockReason(){return ReadOnly?"Read-only target: runtime controls disabled":OwnerPipe==null?"Unsupported: no verified owning tray endpoint; open through the selected tray for lifecycle controls":null;}
  internal void RequireServiceControl(){string reason=ServiceBlockReason();if(reason!=null)throw new InvalidOperationException(reason);}
  internal string ServiceCommand(string operation){RequireServiceControl();return BridgeOwnerChannel.Request(this,operation);}
 }

 internal sealed class ConfigHelperClient {
  readonly GuiLaunchOptions options;
  internal ConfigHelperClient(GuiLaunchOptions target){options=target;}
  internal string Run(string config,string arguments,int timeoutMs){
   options.VerifyHelper();
   var start=new ProcessStartInfo(options.ConfigHelper,GuiLaunchOptions.Quote(config)+" "+arguments){UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true};
   var output=new StringBuilder();var error=new StringBuilder();
   using(var process=new Process{StartInfo=start}){
    process.OutputDataReceived+=delegate(object s,DataReceivedEventArgs e){if(e.Data!=null)lock(output){if(output.Length<8192)output.AppendLine(e.Data.Substring(0,Math.Min(e.Data.Length,8192-output.Length)));}};
    process.ErrorDataReceived+=delegate(object s,DataReceivedEventArgs e){if(e.Data!=null)lock(error){if(error.Length<8192)error.AppendLine(e.Data.Substring(0,Math.Min(e.Data.Length,8192-error.Length)));}};
    process.Start();process.BeginOutputReadLine();process.BeginErrorReadLine();
    if(!process.WaitForExit(timeoutMs)){
     int ownedId=process.Id;bool stopped=false;
     try{process.Kill();stopped=process.WaitForExit(2000);}catch(InvalidOperationException){stopped=true;}
     throw new InvalidOperationException("Helper outcome unconfirmed (timeout), owned child PID "+ownedId+(stopped?" terminated/exited":" may still be running")+". Reload and review before another save. No service was signalled.");
    }
    process.WaitForExit();
    if(process.ExitCode!=0)throw new InvalidOperationException("Config helper rejected operation (exit "+process.ExitCode+"): "+error.ToString().Trim());
    return output.ToString();
   }
  }
  internal void CheckCompatibility(){
   if(Run(options.ConfigFile,"transaction-version",5000).Trim()!="monaka-config-transaction-v1")throw new InvalidOperationException("Unsupported config helper transaction generation");
  }
  internal void Command(string arguments){options.RequireEdit();CheckCompatibility();Run(options.ConfigFile,arguments,15000);}
  internal void TransformAndCommit(byte[] expected,string arguments,bool migration,CancellationToken cancel){
   options.RequireEdit();cancel.ThrowIfCancellationRequested();
   string directory=Path.Combine(Path.GetTempPath(),"monaka-config-operation-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(directory);
   string working=Path.Combine(directory,"working.json"),candidate=migration?Path.Combine(directory,"candidate.json"):working;
   try{
    File.WriteAllBytes(working,expected);CheckCompatibility();cancel.ThrowIfCancellationRequested();
    Run(working,arguments+(migration?" "+GuiLaunchOptions.Quote(candidate):""),15000);cancel.ThrowIfCancellationRequested();
    Commit(expected,candidate,cancel);
   }finally{
    // Only files in this freshly created private directory belong to this operation.
    foreach(string file in Directory.GetFiles(directory))File.Delete(file);Directory.Delete(directory,false);
   }
  }
  internal void Commit(byte[] expected,string candidate,CancellationToken cancel){
   Commit(expected,candidate,cancel,null);
  }
  internal void Commit(byte[] expected,string candidate,CancellationToken cancel,Action beforeNativeCommit){
   options.RequireEdit();cancel.ThrowIfCancellationRequested();CheckCompatibility();cancel.ThrowIfCancellationRequested();
   Run(candidate,"validate",5000);cancel.ThrowIfCancellationRequested();
   // An older transaction-capable helper may ignore runtime_contract. Prove the
   // selected target extension is actually validated, without touching the target.
   var document=new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(File.ReadAllText(candidate,Encoding.UTF8));
   var mappings=document["mappings"] as IList;
   if(mappings!=null)foreach(Dictionary<string,object> mapping in mappings)if(mapping.ContainsKey("runtime_contract")){
    var probe=MappingEditor.Clone(document);foreach(Dictionary<string,object> row in (IList)probe["mappings"])if(row.ContainsKey("runtime_contract"))((Dictionary<string,object>)row["runtime_contract"])["input_declaration"]=new[]{"ws01a.unsupported.channel"};
    string path=candidate+".contract-probe";
    try{File.WriteAllText(path,new JavaScriptSerializer().Serialize(probe),new UTF8Encoding(false));bool rejected=false;
     try{Run(path,"validate",5000);}catch(InvalidOperationException e){if(e.Message.Contains("rejected operation"))rejected=true;else throw;}
     if(!rejected)throw new InvalidOperationException("Config helper ignores runtime_contract; select the matching toolset");
    }finally{if(File.Exists(path))File.Delete(path);}break;
   }
   cancel.ThrowIfCancellationRequested();
   string snapshot=candidate+".expected";
   try{File.WriteAllBytes(snapshot,expected);cancel.ThrowIfCancellationRequested();
    // Cancellation is checked before commit. Once native commit starts, its
    // actual result decides SAVED/REJECTED/UNKNOWN; no rollback or fake cancel.
    if(beforeNativeCommit!=null)beforeNativeCommit();cancel.ThrowIfCancellationRequested();
    Run(options.ConfigFile,"commit "+GuiLaunchOptions.Quote(snapshot)+" "+GuiLaunchOptions.Quote(candidate),15000);
   }finally{if(File.Exists(snapshot))File.Delete(snapshot);}
  }
 }
}
