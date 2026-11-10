import unittest
from analyze_drawing_probe import summarize


def native(kind, seq, x=0, y=0, t=1):
    return f"[drawing-probe] native kind={kind} seq={seq} x={x} y={y} w=400 h=300 t_us={t} (credential redacted)"


def pointer(x, y, t, kind=1):
    return f"[drawing-probe] pointer kind={kind} x={x} y={y} t_us={t}"


class AnalysisTests(unittest.TestCase):
    def test_preview_measurements_are_bounded_and_session_scoped(self):
        lines = [native("O", 1)]
        for x in range(100):
            lines.append(f"[drawing-probe] image_quad x={x} y=200 w=600 h=450 bottom_up=0")
        lines += ["[drawing-probe] local_preview uploaded strokes=3 calibrated=1 full=0 rejected=2",
                  "private unstructured body", native("O", 1, t=300),
                  "[drawing-probe] local_preview uploaded strokes=0 calibrated=0 full=0 rejected=0"]
        result = summarize(lines)
        first, second = result["sessions"]
        self.assertEqual(len(first["image_quad_measurements"]), 64)
        self.assertEqual(first["image_quad_measurements"][0]["rectangle"][0], 36)
        self.assertEqual(first["peak_uploaded_strokes"], 3)
        self.assertTrue(first["local_preview"]["aligned"])
        self.assertEqual(second["peak_uploaded_strokes"], 0)
        self.assertFalse(result["gates_complete"])
        self.assertNotIn("private", str(result))

    def test_world_texture_counts_do_not_certify_rendering_or_leak_across_reset(self):
        lines = [native("O",1), "[drawing-probe] world base verified; sketch-only composite ready",
                 "[drawing-probe] world_canvas uploaded strokes=4", native("O",1,t=200),
                 "[drawing-probe] world_canvas uploaded strokes=0"]
        result = summarize(lines)
        self.assertTrue(result["sessions"][0]["world_base_verified"])
        self.assertEqual(result["sessions"][0]["peak_world_strokes"],4)
        self.assertEqual(result["sessions"][1]["world_uploaded_strokes"],0)
        self.assertFalse(result["gates_complete"])

    def test_known_scaled_mapping_and_redaction(self):
        lines = ["private unrelated line", native("O", 1),
                 "[drawing-probe] gui_scale=1.5 t_us=2", native("G", 2, 100, 200, 3)]
        for i, (x, y) in enumerate([(30, 40), (100, 80), (250, 200)]):
            t = 100_000 + i * 500_000
            lines += [pointer(1.5 * (100 + x) + 12, 1.5 * (200 + y) + 90, t),
                      native("D", i + 3, x, y, t + 20_000)]
        result = summarize(lines)
        session = result["sessions"][0]
        self.assertEqual(session["matched_downs"], 3)
        self.assertAlmostEqual(session["correlation_fit"]["scale"], 1.5)
        self.assertAlmostEqual(session["correlation_fit"]["max_pixels"], 0)
        self.assertFalse(result["gates_complete"])
        self.assertNotIn("private", str(result))

    def test_session_isolation_and_close(self):
        lines = [native("O", 1), native("G", 2, 100, 200, 2),
                 pointer(120, 250, 100_000), native("O", 1, t=100_001),
                 native("D", 2, 20, 50, 100_002), native("C", 3, t=100_003),
                 pointer(130, 250, 100_004), native("D", 4, 30, 50, 100_005)]
        sessions = summarize(lines)["sessions"]
        self.assertEqual(len(sessions), 2)
        self.assertEqual(sessions[1]["unmatched_downs"], 1)
        self.assertEqual(sessions[1]["native_events"]["D"], 1)

    def test_different_gui_scales_are_not_pooled(self):
        lines = [native("O", 1), native("G", 2, 100, 200, 3)]
        seq = 3
        for scale in (1, 1.5):
            lines.append(f"[drawing-probe] gui_scale={scale} t_us={seq * 100_000}")
            for x, y in [(30, 40), (100, 80), (250, 200)]:
                t = seq * 500_000
                lines += [pointer(100 + x + 8 * scale, 200 + y + 69 * scale, t),
                          native("D", seq, x, y, t + 20_000)]
                seq += 1
        session = summarize(lines)["sessions"][0]
        self.assertNotIn("correlation_fit", session)
        self.assertEqual(len(session["fits_by_gui_scale"]), 2)
        for group in session["fits_by_gui_scale"]:
            self.assertAlmostEqual(group["fit"]["scale"], 1)
            self.assertAlmostEqual(group["fit"]["max_pixels"], 0)

    def test_reuse_stale_rapid_and_moving_geometry_rejected(self):
        lines = [native("O", 1), native("G", 2, 100, 200, 2),
                 pointer(120, 250, 100_000), native("D", 3, 20, 50, 120_000),
                 native("D", 4, 20, 50, 130_000),
                 pointer(130, 250, 200_000), pointer(140, 250, 210_000),
                 native("D", 5, 40, 50, 220_000),
                 pointer(140, 250, 300_000), native("D", 6, 40, 50, 600_000),
                 native("G", 7, 150, 250, 700_000), pointer(180, 300, 710_000),
                 native("D", 8, 30, 50, 720_000)]
        s = summarize(lines)["sessions"][0]
        self.assertEqual(s["matched_downs"], 1)
        self.assertEqual(s["ambiguous_downs"], 1)
        self.assertEqual(s["unmatched_downs"], 3)


if __name__ == "__main__":
    unittest.main()
