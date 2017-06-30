<?php
/*
** Zabbix
** Copyright (C) 2001-2017 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
**/


$backurl = (new CUrl('zabbix.php'))
	->setArgument('action', 'dashboard.view');
if ($data['fullscreen'] == 1) {
	$backurl->setArgument('fullscreen', $data['fullscreen']);
}
$backurl = $backurl->getUrl();

$url_details = (new CUrl('tr_events.php'))
	->setArgument('triggerid', '')
	->setArgument('eventid', '');
if ($data['fullscreen'] == 1) {
	$url_details->setArgument('fullscreen', $data['fullscreen']);
}

$show_timeline = true;
$show_recovery_data = in_array($data['fields']['show'], [TRIGGERS_OPTION_RECENT_PROBLEM, TRIGGERS_OPTION_ALL]);

if ($show_timeline) {
	$header = [
		(new CColHeader(_('Time')))->addClass(ZBX_STYLE_RIGHT),
		(new CColHeader())->addClass(ZBX_STYLE_TIMELINE_TH),
		(new CColHeader())->addClass(ZBX_STYLE_TIMELINE_TH)
	];
}
else {
	$header = [(new CColHeader(_('Time')))];
}

$table = (new CTableInfo())
	->setHeader(array_merge($header, [
		$show_recovery_data ? _('Recovery time') : null,
		$show_recovery_data ? _('Status') : null,
		_('Info'),
		_('Host'),
		_('Problem'),
		_('Duration'),
		$data['config']['event_ack_enable'] ? _('Ack') : null,
		_('Actions')
	]));

$today = strtotime('today');
$last_clock = 0;

if ($data['data']['problems']) {
	$triggers_hosts = makeTriggersHostsList($data['data']['triggers_hosts']);
}
if ($data['config']['event_ack_enable']) {
	$acknowledges = makeEventsAcknowledges($data['data']['problems'], $backurl);
}
$actions = makeEventsActions($data['data']['problems'], true);

foreach ($data['data']['problems'] as $eventid => $problem) {
	$trigger = $data['data']['triggers'][$problem['objectid']];

	if ($problem['r_eventid'] != 0) {
		$value = TRIGGER_VALUE_FALSE;
		$value_str = _('RESOLVED');
		$value_clock = $problem['r_clock'];
	}
	else {
		$in_closing = false;

		if ($data['config']['event_ack_enable']) {
			foreach ($problem['acknowledges'] as $acknowledge) {
				if ($acknowledge['action'] == ZBX_ACKNOWLEDGE_ACTION_CLOSE_PROBLEM) {
					$in_closing = true;
					break;
				}
			}
		}

		$value = $in_closing ? TRIGGER_VALUE_FALSE : TRIGGER_VALUE_TRUE;
		$value_str = $in_closing ? _('CLOSING') : _('PROBLEM');
		$value_clock = $in_closing ? time() : $problem['clock'];
	}

	$url_details
		->setArgument('triggerid', $problem['objectid'])
		->setArgument('eventid', $problem['eventid']);

	$cell_clock = ($problem['clock'] >= $today)
		? zbx_date2str(TIME_FORMAT_SECONDS, $problem['clock'])
		: zbx_date2str(DATE_TIME_FORMAT_SECONDS, $problem['clock']);
	$cell_clock = new CCol(new CLink($cell_clock, $url_details));
	if ($show_recovery_data) {
		if ($problem['r_eventid'] != 0) {
			$cell_r_clock = ($problem['r_clock'] >= $today)
				? zbx_date2str(TIME_FORMAT_SECONDS, $problem['r_clock'])
				: zbx_date2str(DATE_TIME_FORMAT_SECONDS, $problem['r_clock']);
			$cell_r_clock = (new CCol(new CLink($cell_r_clock, $url_details)))
				->addClass(ZBX_STYLE_NOWRAP)
				->addClass(ZBX_STYLE_RIGHT);
		}
		else {
			$cell_r_clock = '';
		}

		$cell_status = new CSpan($value_str);

		// Add colors and blinking to span depending on configuration and trigger parameters.
		addTriggerValueStyle($cell_status, $value, $value_clock,
			$data['config']['event_ack_enable'] && (bool) $problem['acknowledges']
		);
	}

	// Info.
	$info_icons = [];
	if ($problem['r_eventid'] != 0) {
		if ($problem['correlationid'] != 0) {
			$info_icons[] = makeInformationIcon(
				array_key_exists($problem['correlationid'], $data['correlations'])
					? _s('Resolved by correlation rule "%1$s".',
						$data['correlations'][$problem['correlationid']]['name']
					)
					: _('Resolved by correlation rule.')
			);
		}
		elseif ($problem['userid'] != 0) {
			$info_icons[] = makeInformationIcon(
				array_key_exists($problem['userid'], $data['data']['users'])
					? _s('Resolved by user "%1$s".', getUserFullname($data['data']['users'][$problem['userid']]))
					: _('Resolved by user.')
			);
		}
	}

	$description = [
		(new CSpan(CMacrosResolverHelper::resolveEventDescription(
			$trigger + ['clock' => $problem['clock'], 'ns' => $problem['ns']]
		)))
			->setHint(make_popup_eventlist($trigger, $eventid, $backurl, $data['config'], $data['fullscreen']),
				'', true, 'max-width: 500px'
			)
			->addClass(ZBX_STYLE_LINK_ACTION)
	];

	if ($show_timeline) {
		if ($last_clock != 0) {
			CScreenProblem::addTimelineBreakpoint($table, $last_clock, $problem['clock'], ZBX_SORT_DOWN);
		}
		$last_clock = $problem['clock'];

		$row = [
			$cell_clock->addClass(ZBX_STYLE_TIMELINE_DATE),
			(new CCol())
				->addClass(ZBX_STYLE_TIMELINE_AXIS)
				->addClass(ZBX_STYLE_TIMELINE_DOT),
			(new CCol())->addClass(ZBX_STYLE_TIMELINE_TD)
		];
	}
	else {
		$row = [
			$cell_clock
				->addClass(ZBX_STYLE_NOWRAP)
				->addClass(ZBX_STYLE_RIGHT)
		];
	}

	$table->addRow(array_merge($row, [
		$show_recovery_data ? $cell_r_clock : null,
		$show_recovery_data ? $cell_status : null,
		makeInformationList($info_icons),
		$triggers_hosts[$trigger['triggerid']],
		getSeverityCell($trigger['priority'], null, $description, $value == TRIGGER_VALUE_FALSE),
		(new CCol(
			($problem['r_eventid'] != 0)
				? zbx_date2age($problem['clock'], $problem['r_clock'])
				: zbx_date2age($problem['clock'])
		))
			->addClass(ZBX_STYLE_NOWRAP),
		$data['config']['event_ack_enable'] ? $acknowledges[$problem['eventid']] : null,
		array_key_exists($eventid, $actions)
			? (new CCol($actions[$eventid]))->addClass(ZBX_STYLE_NOWRAP)
			: ''
	]));
}

$output = [
	'header' => $data['name'],
	'body' => $table->toString(),
	'footer' => (new CList([$data['info'], _s('Updated: %s', zbx_date2str(TIME_FORMAT_SECONDS))]))->toString()
];

if (($messages = getMessages()) !== null) {
	$output['messages'] = $messages->toString();
}

if ($data['user']['debug_mode'] == GROUP_DEBUG_MODE_ENABLED) {
	CProfiler::getInstance()->stop();
	$output['debug'] = CProfiler::getInstance()->make()->toString();
}

echo (new CJson())->encode($output);
