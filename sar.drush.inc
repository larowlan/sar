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
    'options' => array(
      'blocks' => dt('Only perform the search and replace on blocks.'),
      'fields' => dt('Only perform the search and replace on these named fields.'),
    ),
    'arguments' => array(
      'search'  => dt('Existing text.'),
      'replace' => dt('Replacement text.'),
    ),
    'examples' => array(
      'simple' => dt('drush sar devel.example.com www.example.com'),
      'blocks' => dt('drush sar --blocks devel.example.com www.example.com'),
      'fields' => dt('drush sar --fields=field_name,field_other devel.example.com www.example.com'),
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_FULL,
  );

  return $items;
}

/**
 * Implements hook_hook_drush_command_validate().
 */
function drush_sar_replace_validate() {
  $args = drush_get_arguments();
  if (count($args) > 3) {
    drush_set_error('NO_TEXT', dt('You must specify text to search & replace.'));
  }
}

/**
 * Command callback.
 *
 * This callback checks the version of Drupal it's operating on and runs an
 * appropriate function to do the actual work. If an unsupported version is
 * detected, it will exit.
 */
function drush_sar_replace() {

  // Parse our command line options.
  $options = drush_sar_parse_options();

  // If there's a problem yo I'll solve it...
  if (!empty($options['error'])) {
    foreach ($options['error'] as $error) {
      drush_log($error, 'error');
    }
    return 0;
  }

  drush_print(dt('Going perform a search & replace on all text fields in all bundles and all custom blocks on @site', array('@site' => variable_get('site_name', 'Drupal'))));
  drush_print(dt('  !search => !replace', array('!search' => $options['search'], '!replace' => $search['replace'])));
  drush_print(dt(' '));

  $continue = drush_confirm(dt('There is no undo. Have you just created a database backup?'));
  if (empty($continue)) {
    drush_log(dt('Aborting'), 'warning');
    return 0;
  }

  $continue = drush_confirm(dt('There really is no undo. Are you sure this is what you want to do?'));
  if (empty($continue)) {
    drush_log(dt('Aborting'), 'warning');
    return 0;
  }

  switch (drush_drupal_major_version()) {
    case 7:
      return _drush_sar_replace_d7($options);

    case 6:
      return _drush_sar_replace_d6($options);

    default:
      drush_log(dt('This version of Drupal is unsupported.'), 'error');
      return 0;
  }
}

/**
 * Parse CLI args and options and return as an array.
 */
function drush_sar_parse_options() {
  $error = array();

  // Check if there are specific fields to process.
  $fields = drush_get_option('fields');
  if (!empty($fields)) {
    $fields = array_filter(explode(',', $fields));
  }

  // Check if we're processing blocks.
  $blocks = drush_get_option('blocks', 0);

  // The other CLI args.
  $args = drush_get_arguments();
  if (count($args) < 3) {
    $error[] = dt('You need to specify both a string to search and one to replace.');
  }

  // Check some sanities. Refuse to do fields and blocks.
  if (!empty($blocks) && !empty($fields)) {
    $error[] = dt('Cannot process blocks when fields are specified.');
  }
  
  // Return an array of options.
  return array(
    'search' => $args[1],
    'replace' => $args[2],
    'fields' => $fields,
    'blocks' => $blocks,
    'error' => $error,
  );
}

/**
 * Does the search and replace for Drupal 7 sites.
 */
function _drush_sar_replace_d7($options) {
  if (empty($options['blocks'])) {
    // Grab all defined fields, then cycle through them and run update queries.
    $fields = field_info_fields();
    foreach ($fields as $field) {
      // Skip fields that aren't provided by the text module.
      if ($field['module'] != 'text') {
        continue;
      }

      // Check if we've specified fields. If so, only process those.
      if (!empty($options['fields']) && !in_array($field['field_name'], $options['fields'])) {
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
        ->expression($column, "REPLACE(${column}, :search, :replace)", array(':search' => $options['search'], ':replace' => $options['replace']))
        ->execute();
      drush_log(dt('  Updated @bundle.', array('@bundle' => format_plural($updated, '1 bundle', '@count bundles'))), 'ok');

      // Update data in FIELD_LOAD_REVISION.
      $updated = db_update($revision_table)
        ->expression($column, "REPLACE(${column}, :search, :replace)", array(':search' => $options['search'], ':replace' => $options['replace']))
        ->execute();
      drush_log(dt('  Updated @revision.', array('@revision' => format_plural($updated, '1 bundle revision', '@count bundle revisions'))), 'ok');
    }
  }

  // We process blocks if no fields were specified.
  if (empty($options['fields'])) {
    // Do the blocks too.
    $updated = db_update('block_custom')
      ->expression('body', "REPLACE(body, :old, :new)", array(':old' => $options['search'], ':new' => $options['replace']))
      ->execute();
    drush_log(dt('  Updated @block.', array('@block' => format_plural($updated, '1 custom block', '@count custom blocks'))), 'ok');
  }

  return 1;
}

/**
 * Does the search and replace for Drupal 6 sites.
 */
function _drush_sar_replace_d6($options) {
  // Update all node revisions
  drush_log(dt('Processing node revisions'), 'ok');
  db_query("UPDATE {node_revisions} SET body = REPLACE(body, '%s', '%s'), teaser = REPLACE(teaser, '%s', '%s')",
    $options['search'], $options['replace'], $options['search'], $options['replace']);
  drush_log(dt('  Updated @revision.', array('@revision' => format_plural(db_affected_rows(), '1 node revision', '@count node revisions'))), 'ok');

  if (empty($options['fields'])) {
    // Do the blocks too.
    drush_log(dt('Processing custom blocks'), 'ok');
    db_query("UPDATE {boxes} SET body = REPLACE(body, '%s', '%s')", $search, $replace);
    drush_log(dt('  Updated @block.', array('@block' => format_plural(db_affected_rows(), '1 custom block', '@count custom bocks'))), 'ok');
  }

  // Check if CCK is present and perform the replace on text fields (assuming we didn't just want blocks).
  if (module_exists('content') && empty($options['blocks'])) {
    $types = content_types();

    foreach ($types as $type) {
      foreach ($type['fields'] as $field) {
        if ($field['module'] != 'text' || !$field['db_storage']) {
          continue;
        }

        // Check if we've specified fields. If so, only process those.
        if (!empty($options['fields']) && !in_array($field['field_name'], $options['fields'])) {
          continue;
        }

        $info = content_database_info($field);
        $revision_table = $info['table'];
        $column = isset($info['columns']['value']['column']) ? $info['columns']['value']['column'] : NULL;

        if (!empty($column)) {
          // Update data in the field revision table.
          drush_log(dt('Processing field @name', array('@name' => $field['field_name'])), 'ok');
          db_query("UPDATE {${revision_table}} SET ${column} = REPLACE(${column}, '%s', '%s')", $options['search'], $options['replace']);
          drush_log(dt('  Updated @revision.', array('@revision' => format_plural(db_affected_rows(), '1 field revision', '@count field revisions'))), 'ok');
        }
      }
    }
  }

  return 1;
}
