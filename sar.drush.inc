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
      'blocks'   => dt('Only perform the search and replace on blocks.'),
      'bundles'  => dt('Only perform the search and replace on these named bundles.'),
      'fields'   => dt('Only perform the search and replace on these named fields.'),
      'dry-run'  => dt('Only show which changes would be made, do not modify any data.'),
      'show-ids' => dt('Show IDs of the entities that would be changed (Forces dry-run to ON).'),
    ),
    'arguments' => array(
      'search'  => dt('Existing text.'),
      'replace' => dt('Replacement text.'),
    ),
    'examples' => array(
      'simple'    => dt('drush sar devel.example.com www.example.com'),
      'blocks'    => dt('drush sar --blocks devel.example.com www.example.com'),
      'bundles'   => dt('drush sar --bundles=page,article devel.example.com www.example.com'),
      'fields'    => dt('drush sar --fields=field_name,field_other devel.example.com www.example.com'),
      'complex'   => dt('drush sar --bundles=article --fields=field_name,field_other devel.example.com www.example.com'),
      'dry run'   => dt('drush sar --dry-run devel.example.com www.example.com'),
      'show ids'  => dt('drush sar --show-ids devel.example.com www.example.com'),
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
  if (count($args) < 3) {
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

  drush_print(dt('Going to @operate a search & replace on all text fields in all bundles and all custom blocks on @site', array('@site' => variable_get('site_name', 'Drupal'), '@operate' => empty($options['dryrun']) ? 'perform' : 'fake')));
  drush_print(dt('  !search => !replace', array('!search' => $options['search'], '!replace' => $options['replace'])));
  drush_print(dt(' '));

  if (empty($options['dryrun'])) {
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
  }
  else {
    drush_log(dt('Doing a dry run'), 'warning');
  }

  switch (drush_drupal_major_version()) {
    case 7:
      return _drush_sar_replace_d7($options);

    case 6:
      return _drush_sar_replace_d6($options);

    default:
      drush_log(dt('This version of Drupal is not supported.'), 'error');
      return 0;
  }
}

/**
 * Parse CLI args and options and return as an array.
 */
function drush_sar_parse_options() {
  $error = array();

  // Check if there are specific bundles (or node types in D6) to process.
  $bundles = drush_get_option('bundles');
  if (!empty($bundles)) {
    $bundles = array_filter(explode(',', $bundles));
  }

  // Check if there are specific fields to process.
  $fields = drush_get_option('fields');
  if (!empty($fields)) {
    $fields = array_filter(explode(',', $fields));
  }

  // Check if we're processing blocks.
  $blocks = drush_get_option('blocks', 0);

  // Doing a dry run?
  $dryrun = drush_get_option('dry-run', 0);

  // Showing IDs?
  $showid = drush_get_option('show-ids', 0);
  if ($showid) $dryrun = 1;

  // The other CLI args.
  $args = drush_get_arguments();
  if (count($args) < 3) {
    $error[] = dt('You need to specify both a string to search and one to replace.');
  }

  // Check some sanities. Refuse to do bundles/fields and blocks.
  if (!empty($blocks) && (!empty($bundles) || !empty($fields))) {
    $error[] = dt('Cannot process blocks when bundles or fields are specified.');
  }

  // Return an array of options.
  return array(
    'search'  => $args[1],
    'replace' => $args[2],
    'bundles' => $bundles,
    'fields'  => $fields,
    'blocks'  => $blocks,
    'dryrun'  => $dryrun,
    'showid'  => $showid,
    'error  ' => $error,
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

      $bundles = array();
      foreach ($field['bundles'] as $entity => $bundle) {
        $bundles += array_values($bundle);
      }

      // If we specified bundles, only process for these bundles. If this field
      // does not exist on the bundles specified, just skip to the next field.
      if (!empty($options['bundles'])) {
        $bundles = array_intersect($options['bundles'], $bundles);
        if (empty($bundles)) {
          continue;
        }
      }

      // At this stage, $bundles contains either the full list of bundles for
      // this field or the subset we need to operate on, so the update query
      // condition is always OK.

      // Use (private, naughty) API calls to grab the table and column names.
      $data_table = _field_sql_storage_tablename($field);
      $revision_table = _field_sql_storage_revision_tablename($field);
      $column = _field_sql_storage_columnname($field['field_name'], 'value');
      if ($field['type'] == 'text_with_summary') {
        $summary_column = _field_sql_storage_columnname($field['field_name'], 'summary');
      }

      drush_log(dt('Processing field @name (@bundles)', array('@name' => $field['field_name'], '@bundles' => implode(', ', $bundles))), 'ok');

      // Update data in FIELD_LOAD_CURRENT.
      if (empty($options['dryrun'])) {
        $query = db_update($data_table)
          ->expression($column, "REPLACE(${column}, :search, :replace)", array(':search' => $options['search'], ':replace' => $options['replace']));
        // Replace values in summary columns as well.
        if ($field['type'] == 'text_with_summary') {
          $query->expression($summary_column, "REPLACE(${summary_column}, :search, :replace)", array(':search' => $options['search'], ':replace' => $options['replace']));
        }
        $query->condition('bundle', $bundles, 'IN');
        $updated = $query->execute();
      }
      else {
        $or = db_or();
        $or->condition($column, '%' . db_like($options['search']) . '%', 'LIKE');
        if ($field['type'] == 'text_with_summary') {
          $or->condition($summary_column, '%' . db_like($options['search']) . '%', 'LIKE');
        }
        if (empty($options['showid'])) {
          $updated = db_select($data_table, 'd')->fields('d', array('entity_id'))->condition($or)->execute()->rowCount();
        }
        else {
          $updated = db_select($data_table, 'd')->fields('d', array('entity_type', 'entity_id'))->condition($or)->execute();
        }
      }

      if (!empty($options['showid'])) {
        foreach ($updated as $row) {
          drush_print(dt('@entity/@id', array('@entity' => $row->entity_type, '@id' => $row->entity_id)));
        }
      }
      else {
        drush_log(dt('  Updated @bundle.', array('@bundle' => format_plural($updated, '1 bundle', '@count bundles'))), 'ok');
      }

      // Update data in FIELD_LOAD_REVISION.
      if (empty($options['dryrun'])) {
        $query = db_update($revision_table)
          ->expression($column, "REPLACE(${column}, :search, :replace)", array(':search' => $options['search'], ':replace' => $options['replace']));
        // Replace values in summary columns as well.
        if ($field['type'] == 'text_with_summary') {
          $query->expression($summary_column, "REPLACE(${summary_column}, :search, :replace)", array(':search' => $options['search'], ':replace' => $options['replace']));
        }
        $query->condition('bundle', $bundles, 'IN');
        $updated = $query->execute();
      }
      else if (empty($options['showid'])) { // Only run showid on current revisions.
        $or = db_or();
        $or->condition($column, '%' . db_like($options['search']) . '%', 'LIKE');
        if ($field['type'] == 'text_with_summary') {
          $or->condition($summary_column, '%' . db_like($options['search']) . '%', 'LIKE');
        }
        $updated = db_select($revision_table, 'r')->fields('r', array('entity_id'))->condition($or)->execute()->rowCount();
      }

      drush_log(dt('  Updated @revision.', array('@revision' => format_plural($updated, '1 bundle revision', '@count bundle revisions'))), 'ok');
    }
  }

  // We process blocks if no fields were specified.
  if (empty($options['fields'])) {
    // Do the blocks too.
    if (empty($options['dryrun'])) {
      $updated = db_update('block_custom')
        ->expression('body', "REPLACE(body, :old, :new)", array(':old' => $options['search'], ':new' => $options['replace']))
        ->execute();
    }
    else {
      if (empty($options['showid'])) {
        $updated = db_select('block_custom', 'b')->fields('b', array('bid'))->condition('body', '%' . db_like($options['search']) . '%' , 'LIKE')->execute()->rowCount();
      }
      else {
        $updated = db_select('block_custom', 'b')->fields('b', array('bid'))->condition('body', '%' . db_like($options['search']) . '%' , 'LIKE')->execute();
      }
    }

    if (!empty($options['showid'])) {
      foreach ($updated as $row) {
        drush_print(dt('block/@bid', array('@bid' => $row->bid)));
      }
    }
    else {
      drush_log(dt('  Updated @block.', array('@block' => format_plural($updated, '1 custom block', '@count custom blocks'))), 'ok');
    }
  }

  return 1;
}

/**
 * Does the search and replace for Drupal 6 sites.
 */
function _drush_sar_replace_d6($options) {
  // Update all node revisions
  drush_log(dt('Processing node revisions'), 'ok');
  if (empty($options['dryrun'])) {
    $query = "UPDATE {node_revisions} r INNER JOIN {node n} ON (r.nid = n.nid) SET r.body = REPLACE(r.body, '%s', '%s'), r.teaser = REPLACE(r.teaser, '%s', '%s')";
    $args  = array($options['search'], $options['replace'], $options['search'], $options['replace']);
  }
  else {
    $query = "SELECT COUNT(r.vid) FROM {node_revisions} r INNER JOIN {node n} ON (r.nid = n.nid) WHERE r.body LIKE '%%%s%%' OR r.teaser LIKE '%%%s%%'";
    $args  = array($options['search'], $options['search']);
  }

  // If we've limited the node types...
  if (!empty($options['bundles'])) {
    $query .= " AND n.type in (" . db_placeholders($options['bundles'], 'varchar') .")";
    $args   = array_merge($args, $options['bundles']);
  }

  $result = db_query($query, $args);
  if (empty($options['dryrun'])) {
    $count = db_affected_rows();
  }
  else {
    $count = db_result($result);
  }

  drush_log(dt('  Updated @revision.', array('@revision' => format_plural($count, '1 node revision', '@count node revisions'))), 'ok');

  if (empty($options['fields']) && empty($options['bundles'])) {
    // Do the blocks too.
    drush_log(dt('Processing custom blocks'), 'ok');
    if (empty($options['dryrun'])) {
      db_query("UPDATE {boxes} SET body = REPLACE(body, '%s', '%s')", $options['search'], $options['replace']);
      $count = db_affected_rows();
    }
    else {
      $count = db_result(db_query("SELECT COUNT(bid) FROM {boxes} WHERE body LIKE '%%%s%%'", $options['search']));
    }
    drush_log(dt('  Updated @block.', array('@block' => format_plural($count, '1 custom block', '@count custom bocks'))), 'ok');
  }

  // Check if CCK is present and perform the replace on text fields (assuming we didn't just want blocks).
  if (module_exists('content') && empty($options['blocks'])) {
    $types = content_types();

    foreach ($types as $type) {
      // If we specified bundles and the current type doesn't match, just skip.
      if (!empty($options['bundles']) && !in_array($type['type'], $options['bundles'])) {
        continue;
      }

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
          drush_log(dt('Processing field @name in @type', array('@name' => $field['field_name'], '@type' => $type['name'])), 'ok');

          if (empty($options['dryrun'])) {
            $query = "UPDATE {${revision_table}} r";
            if (!empty($options['bundles'])) {
              // If we need to the node type...
              $query .= " INNER JOIN {node} n ON (r.nid = n.nid)";
            }
            $query .= " SET r.${column} = REPLACE(r.${column}, '%s', '%s')";
            $args = array($options['search'], $options['replace']);
          }
          else {
            $query = "SELECT COUNT(r.vid) FROM {${revision_table}} r";
            if (!empty($options['bundles'])) {
              // If we need to the node type...
              $query .= " INNER JOIN {node} n ON (r.nid = n.nid)";
            }
            $args = array();
          }

          if (!empty($options['bundles'])) {
            // Limit the current query to the current node type, which is $type['type'].
            $query .= " WHERE n.type = '%s'";
            $args[] = $type['type'];
          }

          if (empty($options['dryrun'])) {
            db_query($query, $args);
            $count = db_affected_rows();
          }
          else {
            $count = db_result(db_query($query, $args));
          }
          drush_log(dt('  Updated @revision.', array('@revision' => format_plural($count, '1 field revision', '@count field revisions'))), 'ok');
        }
      }
    }
  }

  return 1;
}
