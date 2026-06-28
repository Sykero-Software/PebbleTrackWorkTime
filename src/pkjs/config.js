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
      },
      {
        "type": "select",
        "messageKey": "CFG_IDLE_EXIT_SEC",
        "label": "Return to watchface when idle",
        "description": "Close back to the watchface after this many seconds with no button press in the task list. Off disables it.",
        "defaultValue": "15",
        "options": [
          { "label": "Off", "value": "0" },
          { "label": "10 seconds", "value": "10" },
          { "label": "15 seconds", "value": "15" },
          { "label": "30 seconds", "value": "30" },
          { "label": "60 seconds", "value": "60" }
        ]
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save"
  }
];
