<?php
/**
 * @file
 * Drush plugin to globally search & replace data in ALL text fields.
 *
 * Eg: replace /sites/foo.com/files/ with /sites/bar.org/files/
 */

/**
 * Implements hook_drush_command().
 */
function sar_drush_command() {
  $items['search-and-replace'] = array(
    'aliases' => array('sar'),
    'callback' => 'drush_sar_replace',
    'description' => dt('Replace strings in text fields in all content.'),
    'arguments' => array(
      'search' => dt('Existing text.'),
      'replace' => dt('Replacement text.'),
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_FULL,
  );

  return $items;
}

/**
 * Implements hook_hook_drush_command_validate().
 */
function drush_sar_replace_validate() {
  $args = func_get_args();
  if (empty($args[1])) {
    drush_set_error('NO_TEXT', dt('You must specify text to search & replace.'));
  }
}

/**
 * Command callback.
 */
function drush_sar_replace() {
  list ($search, $replace) = func_get_args();

  drush_print(dt('Going perform a search & replace on all text fields in all bundles and all custom blocks on @site', array('@site' => variable_get('site_name', 'Drupal'))));
  drush_print(dt('  !search => !replace', array('!search' => $search, '!replace' => $replace)));
  drush_print(dt(' '));

  $continue = drush_confirm(dt('There is no undo. Have you just created a database backup?'));
  if (empty($continue)) {
    drush_log(dt('Aborting'), 'warning');
    return 0;
  }

  $continue = drush_confirm(dt('There is no undo. Are you sure this is what you want to do?'));
  if (empty($continue)) {
    drush_log(dt('Aborting'), 'warning');
    return 0;
  }

  // Grab all defined fields, then cycle through them and run update queries.
  $fields = field_info_fields();
  foreach ($fields as $field) {
    // Skip fields that aren't provided by the text module.
    if ($field['module'] != 'text') {
      continue;
    }

    // Use (private, naughty) API calls to grab the table and column names.
    $data_table = _field_sql_storage_tablename($field);
    $revision_table = _field_sql_storage_revision_tablename($field);
    $column = _field_sql_storage_columnname($field['field_name'], 'value');

    $bundles = array();
    foreach ($field['bundles'] as $entity => $bundle) {
      $bundles += array_values($bundle);
    }

    drush_log(dt('Processing field @name (@bundles)', array('@name' => $field['field_name'], '@bundles' => implode(', ', $bundles))), 'ok');

    // Update data in FIELD_LOAD_CURRENT.
    $updated = db_update($data_table)
      ->expression($column, "REPLACE(${column}, :search, :replace)", array(':search' => $search, ':replace' => $replace))
      ->execute();
    drush_log(dt('  Updated @bundle.', array('@bundle' => format_plural($updated, '1 bundle', '@count bundles'))), 'ok');

    // Update data in FIELD_LOAD_REVISION.
    $updated = db_update($revision_table)
      ->expression($column, "REPLACE(${column}, :search, :replace)", array(':search' => $search, ':replace' => $replace))
      ->execute();
    drush_log(dt('  Updated @revision.', array('@revision' => format_plural($updated, '1 bundle revision', '@count bundle revisions'))), 'ok');
  }

  // Do the blocks too.
  $updated = db_update('block_custom')
    ->expression('body', "REPLACE(body, :old, :new)", array(':old' => $search, ':new' => $replace))
    ->execute();
  drush_log(dt('  Updated @block.', array('@block' => format_plural($updated, '1 custom block', '@count custom blocks'))), 'ok');

  return 1;
}
