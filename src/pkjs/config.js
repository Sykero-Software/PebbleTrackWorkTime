module.exports = [
  {
    "type": "heading",
    "defaultValue": "TWT Control"
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Behavior"
      },
      {
        "type": "toggle",
        "messageKey": "CFG_AUTO_RETURN",
        "label": "Return to watchface after selecting a task or Stop",
        "description": "When on, the app closes back to the watchface once the phone confirms your task selection or Stop.",
        "defaultValue": true
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save"
  }
];
