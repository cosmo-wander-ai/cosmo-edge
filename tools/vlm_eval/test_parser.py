import json
import unittest

from tools.vlm_eval.parser import JOINT_PROTOCOL, LEVEL_PROTOCOL, parse_output


class LevelWhitespaceTests(unittest.TestCase):
    def test_trims_only_boundary_whitespace(self):
        for level in ('I', 'II', 'III', 'IV'):
            with self.subTest(level=level):
                raw = json.dumps({'description': 'A scene.', 'safety_level': '\t ' + level + '\n'})
                parsed = parse_output(raw)
                self.assertTrue(parsed['joint_valid'])
                self.assertEqual(parsed['safety_level'], level)

    def test_does_not_repair_case_internal_space_or_types(self):
        for level in (' iv ', 'I V', '', ' \t ', 4, None):
            with self.subTest(level=level):
                parsed = parse_output(json.dumps({'description': 'A scene.', 'safety_level': level}))
                self.assertFalse(parsed['safety_level_valid'])
                self.assertTrue(parsed['description_valid'])


class LevelProtocolTests(unittest.TestCase):
    def test_plain_and_complete_fences_preserve_all_levels(self):
        for level in ('I', 'II', 'III', 'IV'):
            body = json.dumps({'safety_level': level})
            for template in ('{}', '```json\n{}\n```', '```\n{}\n```',
                             ' \n```JSON \r\n{}\r\n```\t '):
                with self.subTest(level=level, template=template):
                    wrapped = '```' in template
                    parsed = parse_output(template.format(body), LEVEL_PROTOCOL)
                    self.assertEqual(parsed['safety_level'], level)
                    self.assertTrue(parsed['protocol_valid'])
                    self.assertTrue(parsed['joint_valid'])
                    self.assertEqual(parsed['strict_valid'], not wrapped)
                    self.assertEqual(parsed['markdown_recovered'], wrapped)
                    self.assertFalse(parsed['description_valid'])

    def test_fence_does_not_allow_repair_or_extra_content(self):
        body = '{"safety_level":"IV"}'
        invalid = [
            'Explanation\n```json\n' + body + '\n```',
            '```json\n' + body + '\n```\nExplanation',
            '```json\n' + body, body + '\n```',
            '```json ' + body + '```',
            '```python\n' + body + '\n```',
            '```jſon\n' + body + '\n```',
            '```json\n' + body + '\n```\n```json\n' + body + '\n```',
            '```json\n{"safety_level":"IV"\n```',
            '```json\n' + body + body + '\n```',
            '```json\n{"safety_level":"I","safety_level":"IV"}\n```',
            '```json\n{"description":"scene","safety_level":"IV"}\n```',
            '```json\n{"safety_level":NaN}\n```',
            '```json\n["IV"]\n```',
        ]
        for raw in invalid:
            with self.subTest(raw=raw):
                parsed = parse_output(raw, LEVEL_PROTOCOL)
                self.assertFalse(parsed['protocol_valid'])
                self.assertFalse(parsed['markdown_recovered'])
                self.assertEqual(parsed['error'], 'parse_failure')

    def test_unknown_and_malformed_levels_never_become_normal(self):
        for value in (None, '', 'iv', 'I V', 4, True, [], {}):
            for fenced in (False, True):
                with self.subTest(value=value, fenced=fenced):
                    raw = json.dumps({'safety_level': value})
                    if fenced:
                        raw = '```json\n' + raw + '\n```'
                    parsed = parse_output(raw, LEVEL_PROTOCOL)
                    self.assertFalse(parsed['protocol_valid'])
                    self.assertIsNone(parsed['safety_level'])
                    self.assertEqual(parsed['error'], 'invalid_fields')
        self.assertEqual(parse_output('{}', LEVEL_PROTOCOL)['error'], 'invalid_fields')
        for raw in (None, '', ' \n\t', 4):
            self.assertEqual(parse_output(raw, LEVEL_PROTOCOL)['error'], 'empty_output')

    def test_legacy_contract_remains_strict(self):
        body = '{"description":"scene","safety_level":"IV"}'
        self.assertTrue(parse_output(body, JOINT_PROTOCOL)['joint_valid'])
        self.assertEqual(parse_output('```json\n' + body + '\n```')['error'], 'parse_failure')
        self.assertFalse(parse_output('{"safety_level":"IV"}')['joint_valid'])

    def test_non_json_outer_whitespace_is_not_silently_removed(self):
        for protocol, body in ((JOINT_PROTOCOL, '{"description":"scene","safety_level":"IV"}'),
                               (LEVEL_PROTOCOL, '{"safety_level":"IV"}')):
            with self.subTest(protocol=protocol):
                self.assertEqual(parse_output('\u00a0' + body + '\u00a0', protocol)['error'],
                                 'parse_failure')

    def test_unknown_protocol_rejected(self):
        with self.assertRaisesRegex(ValueError, 'unsupported output protocol'):
            parse_output(None, 'typo')
