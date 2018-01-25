# Drush SAR for Drupal 8

## How do you use this?

Download and enable the module like and other Drupal 8 module.

Use `drush sar node 'replace me' 'new text'` to replace all occurrences of
"replace me" with "new text" in all text fields (node body and field API).

You can uninstall the module when finished.

Be sure to backup your database before running this command, as that is the
only way to undo changes.
The script will remind you to do so of course!
