<?php
/**
 * @file
 * Drush plugin to globally search & replace data in ALL text fields.
 *
 * Eg: replace /sites/foo.com/files/ with /sites/bar.org/files/
 */

use Drupal\Core\Database\Database;
use Drupal\Core\Entity\Sql\SqlContentEntityStorage;
use Drupal\Core\Database\Query\Condition;
use Drupal\text\Plugin\Field\FieldType\TextItemBase;

/**
 * Implements hook_drush_command().
 */
function sar_drush_command() {
  $items['search-and-replace'] = array(
    'aliases' => array('sar'),
    'callback' => 'drush_sar_replace',
    'description' => dt('Replace strings in text fields in all content.'),
    'options' => array(
      'bundles'  => dt('Only perform the search and replace on these named bundles.'),
      'fields'   => dt('Only perform the search and replace on these named fields.'),
      'dry-run'  => dt('Only show which changes would be made, do not modify any data.'),
      'show-ids' => dt('Show IDs of the entities that would be changed (Forces dry-run to ON).'),
    ),
    'arguments' => array(
      'entity_type_id' => dt('Entity type ID.'),
      'search'  => dt('Existing text.'),
      'replace' => dt('Replacement text.'),
    ),
    'examples' => array(
      'simple'    => dt('drush sar node devel.example.com www.example.com'),
      'blocks'    => dt('drush sar block_content devel.example.com www.example.com'),
      'bundles'   => dt('drush sar node --bundles=page,article devel.example.com www.example.com'),
      'fields'    => dt('drush sar node --fields=field_name,field_other devel.example.com www.example.com'),
      'complex'   => dt('drush sar node --bundles=article --fields=field_name,field_other devel.example.com www.example.com'),
      'dry run'   => dt('drush sar node --dry-run devel.example.com www.example.com'),
      'show ids'  => dt('drush sar node --show-ids devel.example.com www.example.com'),
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
  if (count($args) < 4) {
    drush_set_error('NO_TEXT', dt('You must specify text to search & replace and the entity type ID (e.g. node).'));
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

  drush_print(dt('Going to @operate a search & replace on text fields of entity type @type on @site', array(
    '@site' => \Drupal::config('system.site')->get('name'),
    '@operate' => empty($options['dryrun']) ? 'perform' : 'fake',
    '@type' => $options['entity_type_id'],
  )));
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
    case 8:
      return _drush_sar_replace_d8($options);

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

  // Doing a dry run?
  $dryrun = drush_get_option('dry-run', 0);

  // Showing IDs?
  $showid = drush_get_option('show-ids', 0);
  if ($showid) {
    $dryrun = 1;
  }

  // The other CLI args.
  $args = drush_get_arguments();
  if (count($args) < 3) {
    $error[] = dt('You need to specify both a string to search and one to replace.');
  }

  // Check some sanities. Refuse to do bundles/fields and blocks.
  if (!empty($blocks) && (!empty($bundles) || !empty($fields))) {
    $error[] = dt('Cannot process blocks when bundles or fields are specified.');
  }

  $entity_type_manager = \Drupal::entityTypeManager();
  $entity_type_id = $args[1];
  if (!$entity_type_manager->hasDefinition($entity_type_id)) {
    $error[] = dt('No such entity type ID @type', ['@type' => $entity_type_id]);
  }
  $storage = $entity_type_manager->getStorage($entity_type_id);
  if (!$storage instanceof SqlContentEntityStorage) {
    $error[] = dt('The entity type @type does not use SQL storage and cannot use Drush SAR.', ['@type' => $entity_type_id]);
  }
  if ($bundles) {
    /** @var \Drupal\Core\Entity\EntityTypeBundleInfoInterface $bundle_info */
    $bundle_info = \Drupal::service('entity_type.bundle.info');
    $valid_bundles = $bundle_info->getBundleInfo($entity_type_id);
    $invalid = array_diff($bundles, array_keys($valid_bundles));
    if ($invalid) {
      $errors[] = \Drupal::translation()->formatPlural(count($invalid), 'There is no @bundle bundle for the @type entity type', 'The following bundles do not exist for the @type entity type @bundles', [
        '@bundle' => reset($invalid),
        '@bundles' => implode(', ', $invalid),
        '@type' => $entity_type_id,
      ]);
    }
  }

  // Return an array of options.
  return array(
    'search'  => $args[2],
    'replace' => $args[3],
    'entity_type_id' => $entity_type_id,
    'bundles' => $bundles,
    'fields'  => $fields,
    'dryrun'  => $dryrun,
    'showid'  => $showid,
    'error  ' => $error,
  );
}

/**
 * Does the search and replace for Drupal 8 sites.
 */
function _drush_sar_replace_d8($options) {

  // Grab all defined fields, then cycle through them and run update queries.
  $entity_type_manager = \Drupal::entityTypeManager();
  $entity_type_id = $options['entity_type_id'];
  /** @var \Drupal\Core\Entity\Sql\SqlEntityStorageInterface $storage */
  $storage = $entity_type_manager->getStorage($entity_type_id);
  /** @var \Drupal\Core\Entity\EntityFieldManagerInterface $field_manager */
  $field_manager = \Drupal::service('entity_field.manager');
  $field_definitions = $field_manager->getBaseFieldDefinitions($entity_type_id) + $field_manager->getFieldStorageDefinitions($entity_type_id);
  $field_map = $field_manager->getFieldMap()[$entity_type_id];
  /** @var \Drupal\Core\Entity\Sql\DefaultTableMapping $mapping */
  $mapping = $storage->getTableMapping();
  $connection = Database::getConnection();
  /** @var \Drupal\Core\Field\FieldTypePluginManagerInterface $field_plugin_manager */
  $field_plugin_manager = \Drupal::service('plugin.manager.field.field_type');
  /** @var \Drupal\Core\Entity\EntityTypeBundleInfoInterface $bundle_info */
  $bundle_info = \Drupal::service('entity_type.bundle.info');
  $bundles = array_keys($bundle_info->getBundleInfo($entity_type_id));
  /** @var \Drupal\Core\Field\FieldStorageDefinitionInterface[] $field_definitions */
  foreach ($field_map as $field_name => $field) {
    $type = $field_definitions[$field_name]->getType();
    $instance = $field_plugin_manager->getDefinition($type);
    $class_name = $instance['class'];
    $reflection = new \ReflectionClass($class_name);
    // Skip fields that aren't provided by the text module.
    if (!$reflection->isSubclassOf(TextItemBase::class)) {
      continue;
    }

    // Check if we've specified fields. If so, only process those.
    if (!empty($options['fields']) && !in_array($field_name, $options['fields'], TRUE)) {
      continue;
    }

    // If we specified bundles, only process for these bundles. If this field
    // does not exist on the bundles specified, just skip to the next field.
    if (!empty($options['bundles'])) {
      $bundles = array_intersect($options['bundles'], $field['bundles']);
      if (empty($bundles)) {
        drush_log(dt("no bundles"), 'ok');
        continue;
      }
    }

    // At this stage, $bundles contains either the full list of bundles for
    // this field or the subset we need to operate on, so the update query
    // condition is always OK.
    // Use table API to get table info.
    $data_table = $mapping->getDedicatedDataTableName($field_definitions[$field_name]);
    $revision_table = $mapping->getDedicatedRevisionTableName($field_definitions[$field_name]);
    $column = $mapping->getFieldColumnName($field_definitions[$field_name], 'value');
    $summary_column = FALSE;
    if ($field['type'] == 'text_with_summary') {
      $summary_column = $mapping->getFieldColumnName($field_definitions[$field_name], 'summary');
    }

    drush_log(dt('Processing field @name (@bundles)', array(
      '@name' => $field_name,
      '@bundles' => implode(', ', $bundles),
    )), 'ok');

    // Update data in FIELD_LOAD_CURRENT.
    if (empty($options['dryrun'])) {
      $query = $connection->update($data_table)
        ->expression($column, "REPLACE(${column}, :search, :replace)", array(':search' => $options['search'], ':replace' => $options['replace']));
      // Replace values in summary columns as well.
      if ($summary_column) {
        $query->expression($summary_column, "REPLACE(${summary_column}, :search, :replace)", array(':search' => $options['search'], ':replace' => $options['replace']));
      }
      $query->condition('bundle', $bundles, 'IN');
      $updated = $query->execute();
    }
    else {
      $or = new Condition('OR');
      $or->condition($column, '%' . $connection->escapeLike($options['search']) . '%', 'LIKE');
      if ($field['type'] == 'text_with_summary') {
        $or->condition($summary_column, '%' . $connection->escapeLike($options['search']) . '%', 'LIKE');
      }
      if (empty($options['showid'])) {
        $updated = $connection->select($data_table, 'd')->fields('d', array('entity_id'))->condition($or)->execute()->rowCount();
      }
      else {
        $updated = $connection->select($data_table, 'd')->fields('d', array('entity_id'))->condition($or)->execute();
      }
    }

    if (!empty($options['showid'])) {
      foreach ($updated as $row) {
        drush_print(dt('@entity/@id', array('@entity' => $entity_type_id, '@id' => $row->entity_id)));
      }
    }
    else {
      drush_log(dt('  Updated @bundle.', array(
        '@bundle' => \Drupal::translation()->formatPlural($updated, '1 bundle', '@count bundles'),
      )), 'ok');
    }

    // Update data in FIELD_LOAD_REVISION.
    if (empty($options['dryrun'])) {
      $query = $connection->update($revision_table)
        ->expression($column, "REPLACE(${column}, :search, :replace)", array(':search' => $options['search'], ':replace' => $options['replace']));
      // Replace values in summary columns as well.
      if ($field['type'] == 'text_with_summary') {
        $query->expression($summary_column, "REPLACE(${summary_column}, :search, :replace)", array(':search' => $options['search'], ':replace' => $options['replace']));
      }
      $query->condition('bundle', $bundles, 'IN');
      $updated = $query->execute();
    }
    elseif (empty($options['showid'])) {
      // Only run showid on current revisions.
      $or = new Condition('OR');
      $or->condition($column, '%' . $connection->escapeLike($options['search']) . '%', 'LIKE');
      if ($field['type'] == 'text_with_summary') {
        $or->condition($summary_column, '%' . $connection->escapeLike($options['search']) . '%', 'LIKE');
      }
      $updated = $connection->select($revision_table, 'r')->fields('r', array('entity_id'))->condition($or)->execute()->rowCount();
    }

    drush_log(dt('  Updated @revision.', array(
      '@revision' => \Drupal::translation()->formatPlural($options['showid'] ? count($updated) : $updated, '1 bundle revision', '@count bundle revisions'),
    )), 'ok');
  }

  return 1;
}
