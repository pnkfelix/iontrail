/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* appears to have something to do with capturing a camera image. tries to
 * create a dialog, which will fail. TBD. */
var CapturePickerUI = {
  init: function() {
    this.messageManager = Cc["@mozilla.org/parentprocessmessagemanager;1"].getService(Ci.nsIFrameMessageManager);
    this.messageManager.addMessageListener("CapturePicker:Show", this);
  },
  
  receiveMessage: function(aMessage) {
    switch (aMessage.name) {
      case "CapturePicker:Show":
        let params = { result: true };
        let dialog = DialogUI.importModal(null, "chrome://browser/content/prompt/CaptureDialog.xul", params);
        document.getElementById("capturepicker-title").appendChild(document.createTextNode(aMessage.json.title));
        dialog.waitForClose();
        return { value: params.result, path: params.path };
        break;
    }
    // prevents warning from the script loader
    return null;
  }
};
